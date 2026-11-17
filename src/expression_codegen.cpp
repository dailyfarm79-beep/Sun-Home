#include "expression_codegen.hpp"

#include <cstdint>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace sun {
namespace {

std::string cpp_string(const std::string& value) {
    std::ostringstream result;
    result << '"';
    for (unsigned char c : value) {
        switch (c) {
        case '\\': result << "\\\\"; break;
        case '"': result << "\\\""; break;
        case '\n': result << "\\n"; break;
        case '\t': result << "\\t"; break;
        case '\r': result << "\\r"; break;
        default:
            if (c < 32 || c >= 127) {
                const char* digits = "0123456789abcdef";
                result << "\\x" << digits[c >> 4] << digits[c & 0x0f] << "\"\"";
            } else {
                result << static_cast<char>(c);
            }
        }
    }
    result << '"';
    return result.str();
}

} // namespace

std::string emit_expression(const Expression& expression) {
    const auto child = [&](std::size_t index) {
        return emit_expression(expression.children.at(index));
    };
    const auto arguments = [&](std::size_t start) {
        std::ostringstream result;
        for (std::size_t i = start; i < expression.children.size(); ++i) {
            if (i != start) result << ", ";
            result << emit_expression(expression.children[i]);
        }
        return result.str();
    };
    switch (expression.kind) {
    case Expression::Kind::literal:
        if (expression.type == Type::integer) {
            const auto min = std::numeric_limits<std::int64_t>::min();
            if (expression.value == std::to_string(min)) {
                return "std::numeric_limits<std::int64_t>::min()";
            }
            return "std::int64_t{" + expression.value + "}";
        }
        return expression.value;
    case Expression::Kind::string_literal:
        return "std::string(" + cpp_string(expression.value) + ", " +
               std::to_string(expression.value.size()) + ")";
    case Expression::Kind::formatted_string: {
        if (expression.children.empty()) return "std::string(\"\", 0)";
        std::ostringstream result;
        for (std::size_t i = 0; i < expression.children.size(); ++i) {
            if (i) result << " + ";
            result << child(i);
        }
        return result.str();
    }
    case Expression::Kind::string_interpolation:
        return "sun_to_string(" + child(0) + ")";
    case Expression::Kind::variable: return expression.value;
    case Expression::Kind::unary:
        if (expression.value == "sun_int_neg") {
            return "sun_int_neg(" + child(0) + ")";
        }
        return "(" + expression.value + "(" + child(0) + "))";
    case Expression::Kind::binary:
        return "((" + child(0) + ") " + expression.value + " (" + child(1) + "))";
    case Expression::Kind::call:
        return expression.value + "(" + arguments(0) + ")";
    case Expression::Kind::member:
        return "(" + child(0) + ")." + expression.value;
    case Expression::Kind::member_call:
        return "(" + child(0) + ")." + expression.value + "(" + arguments(1) + ")";
    case Expression::Kind::list:
    case Expression::Kind::aggregate: {
        std::ostringstream result;
        if (expression.kind == Expression::Kind::list) {
            result << "std::vector<" << expression.value << ">{";
        } else {
            result << expression.value << '{';
        }
        result << arguments(0) << '}';
        return result.str();
    }
    case Expression::Kind::dictionary: {
        const auto separator = expression.value.find('|');
        if (separator == std::string::npos) {
            throw std::logic_error("dictionary expression is missing its type");
        }
        std::ostringstream result;
        result << "std::map<" << expression.value.substr(0, separator) << ", "
               << expression.value.substr(separator + 1) << ">{";
        for (std::size_t i = 0; i < expression.children.size(); i += 2) {
            if (i) result << ", ";
            result << '{' << emit_expression(expression.children[i]) << ", "
                   << emit_expression(expression.children[i + 1]) << '}';
        }
        result << '}';
        return result.str();
    }
    case Expression::Kind::cast:
        return "static_cast<" + expression.value + ">(" + child(0) + ")";
    case Expression::Kind::empty_list: return "{}";
    case Expression::Kind::empty_dictionary: return "{}";
    }
    throw std::logic_error("invalid expression node");
}

} // namespace sun
