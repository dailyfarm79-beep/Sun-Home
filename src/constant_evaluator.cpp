#include "constant_evaluator.hpp"

#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace sun {
namespace {

using ConstantValue = std::variant<std::int64_t, double, bool, std::string>;

Expression make_expression(Expression::Kind kind, Type type, std::string value,
                           std::vector<Expression> children = {}) {
    return {kind, type, std::move(value), std::move(children)};
}

std::string constant_number_cpp(double value) {
    std::ostringstream result;
    result << std::setprecision(std::numeric_limits<double>::max_digits10) << value;
    auto text = result.str();
    if (text.find_first_of(".eE") == std::string::npos) text += ".0";
    return text;
}

Expression constant_expression(Type type, const ConstantValue& value) {
    if (const auto* integer = std::get_if<std::int64_t>(&value)) {
        return make_expression(Expression::Kind::literal, type, std::to_string(*integer));
    }
    if (const auto* number = std::get_if<double>(&value)) {
        return make_expression(Expression::Kind::literal, type, constant_number_cpp(*number));
    }
    if (const auto* boolean = std::get_if<bool>(&value)) {
        const auto text = *boolean ? "true" : "false";
        return make_expression(Expression::Kind::literal, type, text);
    }
    return make_expression(Expression::Kind::string_literal, type,
                           std::get<std::string>(value));
}

bool is_single_utf8_character(const std::string& value) {
    if (value.empty()) return false;
    const auto first = static_cast<unsigned char>(value[0]);
    std::size_t length = 0;
    if (first <= 0x7f) length = 1;
    else if (first >= 0xc2 && first <= 0xdf) length = 2;
    else if (first >= 0xe0 && first <= 0xef) length = 3;
    else if (first >= 0xf0 && first <= 0xf4) length = 4;
    else return false;
    if (value.size() != length) return false;
    for (std::size_t i = 1; i < length; ++i) {
        const auto byte = static_cast<unsigned char>(value[i]);
        if (byte < 0x80 || byte > 0xbf) return false;
    }
    if (length >= 3) {
        const auto second = static_cast<unsigned char>(value[1]);
        if ((first == 0xe0 && second < 0xa0) ||
            (first == 0xed && second > 0x9f) ||
            (first == 0xf0 && second < 0x90) ||
            (first == 0xf4 && second > 0x8f)) return false;
    }
    return true;
}


std::optional<ConstantValue> evaluate_constant(const Expression& expression) {
    const auto child = [&](std::size_t index) {
        return index < expression.children.size()
                   ? evaluate_constant(expression.children[index])
                   : std::optional<ConstantValue>{};
    };
    const auto numeric_value = [](const ConstantValue& value) -> std::optional<double> {
        if (const auto* number = std::get_if<double>(&value)) return *number;
        if (const auto* integer = std::get_if<std::int64_t>(&value)) {
            return static_cast<double>(*integer);
        }
        return std::nullopt;
    };
    const auto finite_number = [](double value) -> std::optional<ConstantValue> {
        if (!std::isfinite(value)) return std::nullopt;
        return ConstantValue{value};
    };
    const auto integer_add = [](std::int64_t left, std::int64_t right)
        -> std::optional<std::int64_t> {
        if ((right > 0 && left > std::numeric_limits<std::int64_t>::max() - right) ||
            (right < 0 && left < std::numeric_limits<std::int64_t>::min() - right)) {
            return std::nullopt;
        }
        return left + right;
    };
    const auto integer_sub = [](std::int64_t left, std::int64_t right)
        -> std::optional<std::int64_t> {
        if ((right < 0 && left > std::numeric_limits<std::int64_t>::max() + right) ||
            (right > 0 && left < std::numeric_limits<std::int64_t>::min() + right)) {
            return std::nullopt;
        }
        return left - right;
    };
    const auto integer_mul = [](std::int64_t left, std::int64_t right)
        -> std::optional<std::int64_t> {
        if (left == 0 || right == 0) return 0;
        const auto min = std::numeric_limits<std::int64_t>::min();
        const auto max = std::numeric_limits<std::int64_t>::max();
        if ((left == -1 && right == min) || (right == -1 && left == min) ||
            (left > 0 && ((right > 0 && left > max / right) ||
                          (right < 0 && right < min / left))) ||
            (left < 0 && ((right > 0 && left < min / right) ||
                          (right < 0 && left < max / right)))) {
            return std::nullopt;
        }
        return left * right;
    };

    switch (expression.kind) {
    case Expression::Kind::literal:
        if (expression.type == Type::boolean) {
            if (expression.value == "true") return ConstantValue{true};
            if (expression.value == "false") return ConstantValue{false};
            return std::nullopt;
        }
        try {
            if (expression.type == Type::integer) {
                std::size_t parsed = 0;
                const auto value = std::stoll(expression.value, &parsed);
                if (parsed == expression.value.size()) return ConstantValue{value};
                return std::nullopt;
            }
            if (expression.type == Type::number) {
                std::size_t parsed = 0;
                const auto value = std::stod(expression.value, &parsed);
                if (parsed == expression.value.size() && std::isfinite(value)) {
                    return ConstantValue{value};
                }
            }
        } catch (const std::invalid_argument&) {
            return std::nullopt;
        } catch (const std::out_of_range&) {
            return std::nullopt;
        }
        return std::nullopt;
    case Expression::Kind::string_literal:
        return ConstantValue{expression.value};
    case Expression::Kind::formatted_string: {
        std::string result;
        for (const auto& part : expression.children) {
            const auto value = evaluate_constant(part);
            if (!value || !std::holds_alternative<std::string>(*value)) {
                return std::nullopt;
            }
            result += std::get<std::string>(*value);
        }
        return ConstantValue{std::move(result)};
    }
    case Expression::Kind::string_interpolation: {
        const auto value = child(0);
        if (!value) return std::nullopt;
        if (const auto* text = std::get_if<std::string>(&*value)) {
            return ConstantValue{*text};
        }
        if (const auto* boolean = std::get_if<bool>(&*value)) {
            return ConstantValue{*boolean ? std::string("true") : std::string("false")};
        }
        if (const auto* integer = std::get_if<std::int64_t>(&*value)) {
            return ConstantValue{std::to_string(*integer)};
        }
        return std::nullopt;
    }
    case Expression::Kind::cast: {
        if (expression.value != "double" || expression.children.size() != 1) {
            return std::nullopt;
        }
        const auto value = child(0);
        if (!value) return std::nullopt;
        const auto number = numeric_value(*value);
        if (!number) return std::nullopt;
        return finite_number(*number);
    }
    case Expression::Kind::unary: {
        const auto value = child(0);
        if (!value) return std::nullopt;
        if (expression.value == "!" && std::holds_alternative<bool>(*value)) {
            return ConstantValue{!std::get<bool>(*value)};
        }
        if (expression.value == "-" && std::holds_alternative<double>(*value)) {
            return finite_number(-std::get<double>(*value));
        }
        return std::nullopt;
    }
    case Expression::Kind::binary: {
        const auto left = child(0);
        if (!left) return std::nullopt;
        if (expression.value == "&&" && std::holds_alternative<bool>(*left) &&
            !std::get<bool>(*left)) {
            return ConstantValue{false};
        }
        if (expression.value == "||" && std::holds_alternative<bool>(*left) &&
            std::get<bool>(*left)) {
            return ConstantValue{true};
        }
        const auto right = child(1);
        if (!right) return std::nullopt;
        if (expression.value == "&&" || expression.value == "||") {
            if (!std::holds_alternative<bool>(*left) ||
                !std::holds_alternative<bool>(*right)) return std::nullopt;
            const auto lhs = std::get<bool>(*left);
            const auto rhs = std::get<bool>(*right);
            return ConstantValue{expression.value == "&&" ? lhs && rhs : lhs || rhs};
        }
        if (expression.value == "+" &&
            std::holds_alternative<std::string>(*left) &&
            std::holds_alternative<std::string>(*right)) {
            return ConstantValue{std::get<std::string>(*left) +
                                 std::get<std::string>(*right)};
        }
        if (expression.type == Type::boolean) {
            bool result = false;
            if (std::holds_alternative<std::int64_t>(*left) &&
                std::holds_alternative<std::int64_t>(*right)) {
                const auto lhs = std::get<std::int64_t>(*left);
                const auto rhs = std::get<std::int64_t>(*right);
                if (expression.value == "==") result = lhs == rhs;
                else if (expression.value == "!=") result = lhs != rhs;
                else if (expression.value == "<") result = lhs < rhs;
                else if (expression.value == "<=") result = lhs <= rhs;
                else if (expression.value == ">") result = lhs > rhs;
                else if (expression.value == ">=") result = lhs >= rhs;
                else return std::nullopt;
                return ConstantValue{result};
            }
            if (std::holds_alternative<bool>(*left) &&
                std::holds_alternative<bool>(*right)) {
                if (expression.value == "==") {
                    return ConstantValue{std::get<bool>(*left) == std::get<bool>(*right)};
                }
                if (expression.value == "!=") {
                    return ConstantValue{std::get<bool>(*left) != std::get<bool>(*right)};
                }
                return std::nullopt;
            }
            if (std::holds_alternative<std::string>(*left) &&
                std::holds_alternative<std::string>(*right)) {
                if (expression.value == "==") {
                    return ConstantValue{std::get<std::string>(*left) ==
                                         std::get<std::string>(*right)};
                }
                if (expression.value == "!=") {
                    return ConstantValue{std::get<std::string>(*left) !=
                                         std::get<std::string>(*right)};
                }
                return std::nullopt;
            }
            const auto lhs = numeric_value(*left);
            const auto rhs = numeric_value(*right);
            if (!lhs || !rhs) return std::nullopt;
            if (expression.value == "==") result = *lhs == *rhs;
            else if (expression.value == "!=") result = *lhs != *rhs;
            else if (expression.value == "<") result = *lhs < *rhs;
            else if (expression.value == "<=") result = *lhs <= *rhs;
            else if (expression.value == ">") result = *lhs > *rhs;
            else if (expression.value == ">=") result = *lhs >= *rhs;
            else return std::nullopt;
            return ConstantValue{result};
        }
        const auto lhs = numeric_value(*left);
        const auto rhs = numeric_value(*right);
        if (!lhs || !rhs) return std::nullopt;
        if (expression.value == "+") return finite_number(*lhs + *rhs);
        if (expression.value == "-") return finite_number(*lhs - *rhs);
        if (expression.value == "*") return finite_number(*lhs * *rhs);
        if (expression.value == "/") {
            if (*rhs == 0.0) return std::nullopt;
            return finite_number(*lhs / *rhs);
        }
        return std::nullopt;
    }
    case Expression::Kind::call: {
        if (expression.value == "sun_to_char" && expression.children.size() == 1) {
            const auto value = child(0);
            if (!value || !std::holds_alternative<std::string>(*value) ||
                !is_single_utf8_character(std::get<std::string>(*value))) {
                return std::nullopt;
            }
            return *value;
        }
        if (expression.value == "sun_to_int" && expression.children.size() == 1) {
            const auto value = child(0);
            if (!value) return std::nullopt;
            const auto number = numeric_value(*value);
            if (!number || !std::isfinite(*number) || std::floor(*number) != *number) {
                return std::nullopt;
            }
            const double limit = std::ldexp(1.0, 63);
            if (*number < -limit || *number >= limit) {
                return std::nullopt;
            }
            return ConstantValue{static_cast<std::int64_t>(*number)};
        }
        if ((expression.value == "sun_int_neg" ||
             expression.value == "sun_int_add" ||
             expression.value == "sun_int_sub" ||
             expression.value == "sun_int_mul" ||
             expression.value == "sun_int_div" ||
             expression.value == "sun_int_mod") &&
            (expression.children.size() == 1 || expression.children.size() == 2)) {
            const auto left = child(0);
            if (!left || !std::holds_alternative<std::int64_t>(*left)) return std::nullopt;
            const auto lhs = std::get<std::int64_t>(*left);
            if (expression.value == "sun_int_neg") {
                if (lhs == std::numeric_limits<std::int64_t>::min()) return std::nullopt;
                return ConstantValue{-lhs};
            }
            const auto right = child(1);
            if (!right || !std::holds_alternative<std::int64_t>(*right)) {
                return std::nullopt;
            }
            const auto rhs = std::get<std::int64_t>(*right);
            if (expression.value == "sun_int_add") {
                const auto value = integer_add(lhs, rhs);
                return value ? std::optional<ConstantValue>{*value} : std::nullopt;
            }
            if (expression.value == "sun_int_sub") {
                const auto value = integer_sub(lhs, rhs);
                return value ? std::optional<ConstantValue>{*value} : std::nullopt;
            }
            if (expression.value == "sun_int_mul") {
                const auto value = integer_mul(lhs, rhs);
                return value ? std::optional<ConstantValue>{*value} : std::nullopt;
            }
            if (rhs == 0) return std::nullopt;
            if (expression.value == "sun_int_div") {
                if (lhs == std::numeric_limits<std::int64_t>::min() && rhs == -1) {
                    return std::nullopt;
                }
                return ConstantValue{lhs / rhs};
            }
            if (lhs == std::numeric_limits<std::int64_t>::min() && rhs == -1) {
                return ConstantValue{std::int64_t{0}};
            }
            return ConstantValue{lhs % rhs};
        }
        const auto evaluate_math = [&](const std::vector<double>& arguments)
            -> std::optional<ConstantValue> {
            double result = 0.0;
            if (expression.value == "std::abs" && arguments.size() == 1) result = std::abs(arguments[0]);
            else if (expression.value == "std::ceil" && arguments.size() == 1) result = std::ceil(arguments[0]);
            else if (expression.value == "std::cos" && arguments.size() == 1) result = std::cos(arguments[0]);
            else if (expression.value == "std::exp" && arguments.size() == 1) result = std::exp(arguments[0]);
            else if (expression.value == "std::floor" && arguments.size() == 1) result = std::floor(arguments[0]);
            else if (expression.value == "std::log" && arguments.size() == 1) result = std::log(arguments[0]);
            else if (expression.value == "std::pow" && arguments.size() == 2) result = std::pow(arguments[0], arguments[1]);
            else if (expression.value == "std::sin" && arguments.size() == 1) result = std::sin(arguments[0]);
            else if (expression.value == "std::sqrt" && arguments.size() == 1) result = std::sqrt(arguments[0]);
            else if (expression.value == "std::tan" && arguments.size() == 1) result = std::tan(arguments[0]);
            else if (expression.value == "std::fmod" && arguments.size() == 2) result = std::fmod(arguments[0], arguments[1]);
            else if (expression.value == "std::round" && arguments.size() == 1) result = std::round(arguments[0]);
            else return std::nullopt;
            return finite_number(result);
        };
        if (expression.value.rfind("std::", 0) == 0) {
            std::vector<double> arguments;
            for (std::size_t i = 0; i < expression.children.size(); ++i) {
                const auto value = child(i);
                if (!value) return std::nullopt;
                const auto number = numeric_value(*value);
                if (!number) return std::nullopt;
                arguments.push_back(*number);
            }
            return evaluate_math(arguments);
        }
        if ((expression.value == "sun_min" || expression.value == "sun_max") &&
            expression.children.size() == 2) {
            const auto left = child(0);
            const auto right = child(1);
            if (!left || !right) return std::nullopt;
            const auto lhs = numeric_value(*left);
            const auto rhs = numeric_value(*right);
            if (!lhs || !rhs) return std::nullopt;
            const bool take_left = expression.value == "sun_min" ? *lhs <= *rhs : *lhs >= *rhs;
            return take_left ? left : right;
        }
        if ((expression.value == "sun_sum_int" ||
             expression.value == "sun_sum_number") &&
            expression.children.size() == 1 &&
            expression.children[0].kind == Expression::Kind::list) {
            if (expression.value == "sun_sum_int") {
                std::int64_t total = 0;
                for (std::size_t i = 0; i < expression.children[0].children.size(); ++i) {
                    const auto value = evaluate_constant(expression.children[0].children[i]);
                    if (!value || !std::holds_alternative<std::int64_t>(*value)) {
                        return std::nullopt;
                    }
                    const auto next = integer_add(total, std::get<std::int64_t>(*value));
                    if (!next) return std::nullopt;
                    total = *next;
                }
                return ConstantValue{total};
            }
            double total = 0.0;
            for (std::size_t i = 0; i < expression.children[0].children.size(); ++i) {
                const auto value = evaluate_constant(expression.children[0].children[i]);
                if (!value) return std::nullopt;
                const auto number = numeric_value(*value);
                if (!number) return std::nullopt;
                total += *number;
                if (!std::isfinite(total)) return std::nullopt;
            }
            return ConstantValue{total};
        }
        if ((expression.value == "sun_all" || expression.value == "sun_any") &&
            expression.children.size() == 1 &&
            expression.children[0].kind == Expression::Kind::list) {
            const bool is_all = expression.value == "sun_all";
            bool result = is_all;
            for (const auto& item : expression.children[0].children) {
                const auto value = evaluate_constant(item);
                if (!value || !std::holds_alternative<bool>(*value)) return std::nullopt;
                if (is_all && !std::get<bool>(*value)) return ConstantValue{false};
                if (!is_all && std::get<bool>(*value)) return ConstantValue{true};
                result = is_all;
            }
            return ConstantValue{result};
        }
        if (expression.value == "sun_to_string" && expression.children.size() == 1) {
            const auto value = child(0);
            if (!value) return std::nullopt;
            if (const auto* text = std::get_if<std::string>(&*value)) {
                return ConstantValue{*text};
            }
            if (const auto* boolean = std::get_if<bool>(&*value)) {
                return ConstantValue{*boolean ? std::string("true") : std::string("false")};
            }
            if (const auto* integer = std::get_if<std::int64_t>(&*value)) {
                return ConstantValue{std::to_string(*integer)};
            }
            return std::nullopt;
        }
        return std::nullopt;
    }
    default:
        return std::nullopt;
    }
}


} // namespace

std::optional<Expression> fold_constant_expression(Type type,
                                                   const Expression& expression) {
    const auto value = evaluate_constant(expression);
    if (!value) return std::nullopt;
    return constant_expression(type, *value);
}

} // namespace sun
