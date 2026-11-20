#include "compiler.hpp"
#include "expression_codegen.hpp"
#include "stdlib_io.hpp"
#include "stdlib_json.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <sstream>

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

void emit_block(std::ostream& out, const Block& statements, std::size_t spaces);

void emit_raw(std::ostream& out, const std::string& code, std::size_t spaces) {
    std::istringstream lines(code);
    const std::string prefix(spaces, ' ');
    std::string line;
    while (std::getline(lines, line)) {
        if (!line.empty()) out << prefix;
        out << line << '\n';
    }
}

void emit_statement(std::ostream& out, const Statement& statement, std::size_t spaces) {
    const std::string prefix(spaces, ' ');
    if (const auto* raw = std::get_if<RawStatement>(&statement.node)) {
        emit_raw(out, raw->cpp, spaces);
    } else if (const auto* declaration =
                   std::get_if<VariableDeclarationStatement>(&statement.node)) {
        out << prefix << (declaration->is_const ? "const " : "")
            << cpp_type(declaration->type) << ' ' << declaration->cpp_name << " = "
            << emit_expression(declaration->initializer) << ";\n";
    } else if (const auto* assignment =
                   std::get_if<AssignmentStatement>(&statement.node)) {
        out << prefix << assignment->cpp_name << " = "
            << emit_expression(assignment->value) << ";\n";
    } else if (const auto* returned = std::get_if<ReturnStatement>(&statement.node)) {
        out << prefix << "return";
        if (returned->value) out << ' ' << emit_expression(*returned->value);
        out << ";\n";
    } else if (const auto* call = std::get_if<CallStatement>(&statement.node)) {
        out << prefix << call->callee << '(';
        for (std::size_t i = 0; i < call->arguments.size(); ++i) {
            if (i) out << ", ";
            out << emit_expression(call->arguments[i]);
        }
        out << ");\n";
    } else if (const auto* call =
                   std::get_if<MemberCallStatement>(&statement.node)) {
        out << prefix << '(' << emit_expression(call->receiver) << ")."
            << call->method << '(';
        for (std::size_t i = 0; i < call->arguments.size(); ++i) {
            if (i) out << ", ";
            out << emit_expression(call->arguments[i]);
        }
        out << ");\n";
    } else if (const auto* assignment =
                   std::get_if<IndexedAssignmentStatement>(&statement.node)) {
        if (assignment->kind == IndexedAssignmentStatement::Kind::list) {
            out << prefix << emit_expression(assignment->object) << " = "
                << emit_expression(assignment->value) << ";\n";
        } else if (assignment->kind ==
                   IndexedAssignmentStatement::Kind::dictionary) {
            out << prefix << emit_expression(assignment->object) << '['
                << emit_expression(assignment->key) << "] = "
                << emit_expression(assignment->value) << ";\n";
        } else if (assignment->kind == IndexedAssignmentStatement::Kind::json) {
            out << prefix << "sun_json_set(" << emit_expression(assignment->object)
                << ", " << emit_expression(assignment->key) << ", sun_json_from("
                << emit_expression(assignment->value) << "));\n";
        } else {
            out << prefix << "sun_bytes_set(" << emit_expression(assignment->object)
                << ", " << emit_expression(assignment->key) << ", "
                << emit_expression(assignment->value) << ");\n";
        }
    } else if (const auto* assignment =
                   std::get_if<MemberAssignmentStatement>(&statement.node)) {
        out << prefix << emit_expression(assignment->target) << " = "
            << emit_expression(assignment->value) << ";\n";
    } else if (const auto* control =
                   std::get_if<LoopControlStatement>(&statement.node)) {
        out << prefix
            << (control->kind == LoopControlStatement::Kind::break_ ? "break;\n"
                                                                    : "continue;\n");
    } else if (const auto* print = std::get_if<PrintStatement>(&statement.node)) {
        out << prefix << "std::cout << sun_to_string(" << emit_expression(print->value)
            << ") << '\\n';\n";
    } else if (const auto* print = std::get_if<PrintColorStatement>(&statement.node)) {
        out << prefix << "std::cout << " << cpp_string(print->color_prefix)
            << " << " << emit_expression(print->value)
            << " << \"\\x1b[0m\" << '\\n';\n";
    } else if (const auto* assertion = std::get_if<AssertStatement>(&statement.node)) {
        out << prefix << "if (!(" << emit_expression(assertion->condition)
            << ")) throw SunRuntimeError(\"assertion failed\");\n";
    } else if (const auto* raised = std::get_if<RaiseStatement>(&statement.node)) {
        out << prefix << "throw " << emit_expression(raised->error) << ";\n";
    } else if (const auto* assignment =
                   std::get_if<DictionaryAssignmentStatement>(&statement.node)) {
        out << prefix << emit_expression(assignment->dictionary) << '['
            << emit_expression(assignment->key) << "] = "
            << emit_expression(assignment->value) << ";\n";
    } else if (const auto* conditional = std::get_if<IfStatement>(&statement.node)) {
        for (std::size_t i = 0; i < conditional->branches.size(); ++i) {
            const auto& branch = conditional->branches[i];
            if (i == 0) out << prefix << "if (";
            else out << "else if (";
            out << emit_expression(branch.condition) << ") {\n";
            emit_block(out, branch.body, spaces + 4);
            out << prefix << '}';
            if (i + 1 < conditional->branches.size() || conditional->else_body) out << ' ';
        }
        if (conditional->else_body) {
            out << "else {\n";
            emit_block(out, *conditional->else_body, spaces + 4);
            out << prefix << '}';
        }
        out << '\n';
    } else if (const auto* matched = std::get_if<MatchStatement>(&statement.node)) {
        out << prefix << "{\n"
            << std::string(spaces + 4, ' ') << "auto "
            << matched->temporary_name << " = " << emit_expression(matched->value)
            << ";\n";
        for (std::size_t i = 0; i < matched->cases.size(); ++i) {
            const auto& match_case = matched->cases[i];
            const auto branch_prefix = std::string(spaces + 4, ' ');
            if (i == 0) out << branch_prefix;
            else out << "else ";
            if (!match_case.enum_variant_type.empty()) {
                const auto pointer = "sun_match_variant_" +
                    std::to_string(statement.line) + '_' + std::to_string(i);
                out << "if (auto* " << pointer << " = std::get_if<"
                    << match_case.enum_variant_type << ">(&"
                    << matched->temporary_name << ")) {\n";
                for (const auto& binding : match_case.bindings) {
                    out << std::string(spaces + 8, ' ') << "auto& "
                        << binding.cpp_name << " = " << pointer << "->"
                        << binding.field_name << ";\n";
                }
            } else {
                out << "if (" << matched->temporary_name << " == "
                    << emit_expression(match_case.pattern) << ") {\n";
            }
            emit_block(out, match_case.body, spaces + 8);
            out << branch_prefix << '}';
            if (i + 1 < matched->cases.size() || matched->else_body) out << ' ';
        }
        if (matched->else_body) {
            out << "else {\n";
            emit_block(out, *matched->else_body, spaces + 8);
            out << std::string(spaces + 4, ' ') << '}';
        }
        out << '\n' << prefix << "}\n";
    } else if (const auto* local_function =
                   std::get_if<LocalFunctionStatement>(&statement.node)) {
        const auto return_type = local_function->return_type
                                     ? cpp_type(*local_function->return_type)
                                     : "void";
        out << prefix << "std::function<" << return_type << '(';
        for (std::size_t i = 0; i < local_function->parameters.size(); ++i) {
            if (i) out << ", ";
            out << cpp_type(local_function->parameters[i].type);
        }
        out << ")> " << local_function->cpp_name << ";\n"
            << prefix << local_function->cpp_name << " = [&]( ";
        for (std::size_t i = 0; i < local_function->parameters.size(); ++i) {
            if (i) out << ", ";
            out << cpp_type(local_function->parameters[i].type) << " sun_arg_"
                << local_function->parameters[i].name;
        }
        out << ") -> " << return_type << " {\n";
        emit_block(out, local_function->body, spaces + 4);
        out << prefix << "};\n";
    } else if (const auto* loop = std::get_if<WhileStatement>(&statement.node)) {
        out << prefix << "while (" << emit_expression(loop->condition) << ") {\n";
        emit_block(out, loop->body, spaces + 4);
        out << prefix << "}\n";
    } else if (const auto* loop = std::get_if<RangeForStatement>(&statement.node)) {
        const auto inner = std::string(spaces + 4, ' ');
        out << prefix << "{\n"
            << inner << "const double " << loop->start_name << " = "
            << emit_expression(loop->start_value) << ";\n"
            << inner << "const double " << loop->stop_name << " = "
            << emit_expression(loop->stop_value) << ";\n"
            << inner << "const double " << loop->step_name << " = "
            << emit_expression(loop->step_value) << ";\n"
            << inner << "if (" << loop->step_name
            << " == 0.0) throw SunRuntimeError(\"range step cannot be zero\");\n"
            << inner << "for (double " << loop->variable_name << " = "
            << loop->start_name << "; (" << loop->step_name << " > 0.0 ? "
            << loop->variable_name << " < " << loop->stop_name << " : "
            << loop->variable_name << " > " << loop->stop_name << "); "
            << loop->variable_name << " = sun_range_next(" << loop->variable_name
            << ", " << loop->step_name << ")) {\n";
        emit_block(out, loop->body, spaces + 8);
        out << inner << "}\n" << prefix << "}\n";
    } else if (const auto* loop = std::get_if<ForEachStatement>(&statement.node)) {
        if (loop->dictionary) {
            out << prefix << "for (const auto& " << loop->entry_name << " : "
                << emit_expression(loop->collection) << ") {\n"
                << std::string(spaces + 4, ' ') << "auto " << loop->variable_name
                << " = " << loop->entry_name << ".first;\n";
        } else {
            out << prefix << "for (auto " << loop->variable_name << " : "
                << emit_expression(loop->collection) << ") {\n";
        }
        emit_block(out, loop->body, spaces + 4);
        out << prefix << "}\n";
    } else if (const auto* opened = std::get_if<WithOpenStatement>(&statement.node)) {
        out << prefix << "{\n"
            << std::string(spaces + 4, ' ') << "SunFileGuard "
            << opened->guard_name << "(sun_io_open("
            << emit_expression(opened->path) << ", "
            << emit_expression(opened->mode) << "));\n"
            << std::string(spaces + 4, ' ') << "const std::int64_t "
            << opened->handle_name << " = " << opened->guard_name
            << ".handle();\n";
        emit_block(out, opened->body, spaces + 4);
        out << prefix << "}\n";
    } else if (const auto* attempt = std::get_if<TryCatchStatement>(&statement.node)) {
        if (attempt->finally_body) {
            out << prefix << "{\n"
                << std::string(spaces + 4, ' ')
                << "SunFinallyGuard sun_finally_guard([&]() {\n";
            emit_block(out, *attempt->finally_body, spaces + 8);
            out << std::string(spaces + 4, ' ') << "});\n";
        }
        if (attempt->handlers.empty()) {
            emit_block(out, attempt->try_body,
                       spaces + (attempt->finally_body ? 4 : 0));
        } else {
            out << prefix << "try {\n";
            emit_block(out, attempt->try_body, spaces + 4);
            for (const auto& handler : attempt->handlers) {
                out << prefix << "} catch (const " << handler.cpp_error_type << "& "
                    << handler.caught_error_name << ") {\n";
                if (handler.message_name) {
                    out << std::string(spaces + 4, ' ') << "const std::string "
                        << *handler.message_name << " = " << handler.caught_error_name
                        << ".what();\n";
                }
                emit_block(out, handler.body, spaces + 4);
            }
            out << prefix << "}\n";
        }
        if (attempt->finally_body) out << prefix << "}\n";
    }
}

void emit_block(std::ostream& out, const Block& statements, std::size_t spaces) {
    for (const auto& statement : statements) {
        emit_statement(out, statement, spaces);
    }
}

std::string render_block(const Block& statements, std::size_t spaces) {
    std::ostringstream result;
    emit_block(result, statements, spaces);
    return result.str();
}

bool inherits_method(const Program& program, const CustomTypeDefinition& type,
                     const std::string& method_name) {
    auto base_name = type.base_type;
    while (!base_name.empty()) {
        const auto base = std::find_if(
            program.custom_types.begin(), program.custom_types.end(),
            [&](const CustomTypeDefinition& candidate) {
                return candidate.name == base_name;
            });
        if (base == program.custom_types.end()) break;
        if (std::any_of(base->methods.begin(), base->methods.end(),
                        [&](const Function& method) { return method.name == method_name; })) {
            return true;
        }
        base_name = base->base_type;
    }
    return false;
}

std::vector<Field> all_class_fields(const Program& program,
                                    const CustomTypeDefinition& type) {
    std::vector<Field> fields;
    if (!type.base_type.empty()) {
        const auto base = std::find_if(
            program.custom_types.begin(), program.custom_types.end(),
            [&](const CustomTypeDefinition& candidate) {
                return candidate.name == type.base_type;
            });
        if (base != program.custom_types.end()) fields = all_class_fields(program, *base);
    }
    fields.insert(fields.end(), type.fields.begin(), type.fields.end());
    return fields;
}

std::pair<std::string, const Function*> class_initializer(
    const Program& program, const CustomTypeDefinition& type) {
    const auto own = std::find_if(
        type.methods.begin(), type.methods.end(),
        [](const Function& method) { return method.name == "init"; });
    if (own != type.methods.end()) return {type.name, &*own};
    auto base_name = type.base_type;
    while (!base_name.empty()) {
        const auto base = std::find_if(
            program.custom_types.begin(), program.custom_types.end(),
            [&](const CustomTypeDefinition& candidate) {
                return candidate.name == base_name;
            });
        if (base == program.custom_types.end()) break;
        const auto inherited = std::find_if(
            base->methods.begin(), base->methods.end(),
            [](const Function& method) { return method.name == "init"; });
        if (inherited != base->methods.end()) return {base->name, &*inherited};
        base_name = base->base_type;
    }
    return {{}, nullptr};
}

} // namespace

std::string transpile(const Program& program) {
    std::ostringstream out;
    out << "#include <algorithm>\n#include <cctype>\n#include <cmath>\n#include <cstdint>\n#include <cstdlib>\n#include <exception>\n#include <iostream>\n#include <limits>\n"
           "#include <map>\n#include <memory>\n#include <sstream>\n#include <stdexcept>\n"
           "#include <functional>\n#include <string>\n#include <type_traits>\n#include <utility>\n"
           "#include <tuple>\n#include <variant>\n#include <vector>\n";
    if (program.io_imported) {
        out << "#include <filesystem>\n#include <fstream>\n#include <iterator>\n";
    }
    if (program.json_imported) out << "#include <charconv>\n#include <iomanip>\n";
    out << R"cpp(
class SunFinallyGuard {
public:
    explicit SunFinallyGuard(std::function<void()> action)
        : action_(std::move(action)), exceptions_on_entry_(std::uncaught_exceptions()) {}
    SunFinallyGuard(const SunFinallyGuard&) = delete;
    SunFinallyGuard& operator=(const SunFinallyGuard&) = delete;
    ~SunFinallyGuard() noexcept(false) {
        try {
            action_();
        } catch (...) {
            if (std::uncaught_exceptions() > exceptions_on_entry_) return;
            throw;
        }
    }

private:
    std::function<void()> action_;
    int exceptions_on_entry_;
};

class AnError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class SunRuntimeError : public AnError {
public:
    using AnError::AnError;
};

double sun_round_digits(double value, std::int64_t digits) {
    if (digits > 308) return value;
    if (digits < -308) {
        return std::isfinite(value) ? std::copysign(0.0, value) : value;
    }
    const double factor = std::pow(10.0, static_cast<double>(digits));
    return std::round(value * factor) / factor;
}

double sun_math_factorial(double value) {
    if (!std::isfinite(value)) {
        throw SunRuntimeError("math.factorial expects a finite number");
    }
    const auto result = std::tgamma(value + 1.0);
    if (!std::isfinite(result)) {
        throw SunRuntimeError(
            "math.factorial is undefined at negative integers or outside the number range");
    }
    return result;
}

std::string sun_string_upper(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
    return value;
}

std::string sun_string_lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

std::string sun_string_trim(const std::string& value) {
    const auto is_space = [](unsigned char ch) { return std::isspace(ch) != 0; };
    const auto first = std::find_if_not(value.begin(), value.end(), is_space);
    const auto last = std::find_if_not(value.rbegin(), value.rend(), is_space).base();
    return first < last ? std::string(first, last) : std::string{};
}

std::vector<std::string> sun_string_split(const std::string& value,
                                          const std::string& separator) {
    if (separator.empty()) throw SunRuntimeError("string.split separator cannot be empty");
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (true) {
        const auto next = value.find(separator, start);
        if (next == std::string::npos) {
            parts.push_back(value.substr(start));
            break;
        }
        parts.push_back(value.substr(start, next - start));
        start = next + separator.size();
    }
    return parts;
}

std::string sun_string_replace(const std::string& value, const std::string& old_value,
                               const std::string& new_value) {
    if (old_value.empty()) throw SunRuntimeError("string.replace search text cannot be empty");
    std::string result;
    std::size_t start = 0;
    while (true) {
        const auto next = value.find(old_value, start);
        if (next == std::string::npos) {
            result.append(value, start, std::string::npos);
            break;
        }
        result.append(value, start, next - start);
        result += new_value;
        start = next + old_value.size();
    }
    return result;
}

bool sun_string_contains(const std::string& value, const std::string& part) {
    return value.find(part) != std::string::npos;
}

std::int64_t sun_string_count(const std::string& value, const std::string& part) {
    if (part.empty()) throw SunRuntimeError("string.count search text cannot be empty");
    std::int64_t count = 0;
    for (std::size_t position = 0;
         (position = value.find(part, position)) != std::string::npos;
         position += part.size()) {
        if (count == std::numeric_limits<std::int64_t>::max()) {
            throw SunRuntimeError("string.count result exceeds int range");
        }
        ++count;
    }
    return count;
}

bool sun_string_starts_with(const std::string& value, const std::string& prefix) {
    return value.size() >= prefix.size() &&
           value.compare(0, prefix.size(), prefix) == 0;
}

bool sun_string_ends_with(const std::string& value, const std::string& suffix) {
    return value.size() >= suffix.size() &&
           value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

class SunCppException : public std::exception {
public:
    explicit SunCppException(std::string message) : message_(std::move(message)) {}
    const char* what() const noexcept override { return message_.c_str(); }

private:
    std::string message_;
};

using SunType_AnError = AnError;

template <typename Map, typename Key>
decltype(auto) sun_dict_at(Map& values, const Key& key) {
    const auto found = values.find(key);
    if (found == values.end()) throw SunRuntimeError("dictionary key not found");
    return (found->second);
}

template <typename Map, typename Key>
bool sun_dict_contains(const Map& values, const Key& key) {
    return values.find(key) != values.end();
}

template <typename Map, typename Key>
bool sun_dict_remove(Map& values, const Key& key) {
    return values.erase(key) != 0;
}

template <typename Map>
std::vector<typename Map::key_type> sun_dict_keys(const Map& values) {
    std::vector<typename Map::key_type> keys;
    keys.reserve(values.size());
    for (const auto& entry : values) keys.push_back(entry.first);
    return keys;
}

template <typename Map>
std::vector<typename Map::mapped_type> sun_dict_values(const Map& values) {
    std::vector<typename Map::mapped_type> items;
    items.reserve(values.size());
    for (const auto& entry : values) items.push_back(entry.second);
    return items;
}

template <typename Map, typename Key>
typename Map::mapped_type sun_dict_get(
    const Map& values, const Key& key, typename Map::mapped_type fallback) {
    const auto found = values.find(key);
    return found == values.end() ? std::move(fallback) : found->second;
}

std::string sun_input_string(const std::string& prompt) {
    std::cout << prompt << std::flush;
    std::string value;
    if (!std::getline(std::cin, value)) throw SunRuntimeError("input could not read a line");
    return value;
}

double sun_input_number(const std::string& prompt) {
    const auto text = sun_input_string(prompt);
    std::istringstream input(text);
    double value = 0.0;
    if (!(input >> value)) throw SunRuntimeError("input expected a number");
    input >> std::ws;
    if (!input.eof()) throw SunRuntimeError("input expected a number");
    return value;
}

std::int64_t sun_input_int(const std::string& prompt) {
    const auto text = sun_input_string(prompt);
    std::istringstream input(text);
    std::int64_t value = 0;
    if (!(input >> value)) throw SunRuntimeError("input expected an int");
    input >> std::ws;
    if (!input.eof()) throw SunRuntimeError("input expected an int");
    return value;
}

bool sun_input_bool(const std::string& prompt) {
    const auto value = sun_input_string(prompt);
    if (value == "true") return true;
    if (value == "false") return false;
    throw SunRuntimeError("input expected 'true' or 'false'");
}

bool sun_is_one_utf8_character(const std::string& value) {
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

std::string sun_input_char(const std::string& prompt) {
    const auto value = sun_input_string(prompt);
    if (!sun_is_one_utf8_character(value))
        throw SunRuntimeError("input expected one UTF-8 character");
    return value;
}

std::string sun_to_char(const std::string& value) {
    if (!sun_is_one_utf8_character(value))
        throw SunRuntimeError("char() requires one UTF-8 character");
    return value;
}

std::int64_t sun_to_int(double value) {
    const double limit = std::ldexp(1.0, 63);
    if (!std::isfinite(value) || std::floor(value) != value ||
        value < -limit || value >= limit)
        throw SunRuntimeError("int() requires an in-range whole number");
    return static_cast<std::int64_t>(value);
}

std::int64_t sun_int_add(std::int64_t left, std::int64_t right) {
    if ((right > 0 && left > std::numeric_limits<std::int64_t>::max() - right) ||
        (right < 0 && left < std::numeric_limits<std::int64_t>::min() - right))
        throw SunRuntimeError("int arithmetic overflow");
    return left + right;
}

std::int64_t sun_sum_int(const std::vector<std::int64_t>& values) {
    std::int64_t total = 0;
    for (const auto value : values) total = sun_int_add(total, value);
    return total;
}

double sun_sum_number(const std::vector<double>& values) {
    double total = 0.0;
    for (const auto value : values) total += value;
    return total;
}

std::int64_t sun_int_sub(std::int64_t left, std::int64_t right) {
    if ((right < 0 && left > std::numeric_limits<std::int64_t>::max() + right) ||
        (right > 0 && left < std::numeric_limits<std::int64_t>::min() + right))
        throw SunRuntimeError("int arithmetic overflow");
    return left - right;
}

std::int64_t sun_int_mul(std::int64_t left, std::int64_t right) {
    if (left != 0 && right != 0) {
        const auto min = std::numeric_limits<std::int64_t>::min();
        const auto max = std::numeric_limits<std::int64_t>::max();
        if ((left == -1 && right == min) || (right == -1 && left == min) ||
            (left > 0 && ((right > 0 && left > max / right) ||
                          (right < 0 && right < min / left))) ||
            (left < 0 && ((right > 0 && left < min / right) ||
                          (right < 0 && left < max / right))))
            throw SunRuntimeError("int arithmetic overflow");
    }
    return left * right;
}

std::int64_t sun_int_div(std::int64_t left, std::int64_t right) {
    if (right == 0) throw SunRuntimeError("int division by zero");
    if (left == std::numeric_limits<std::int64_t>::min() && right == -1)
        throw SunRuntimeError("int arithmetic overflow");
    return left / right;
}

std::int64_t sun_int_mod(std::int64_t left, std::int64_t right) {
    if (right == 0) throw SunRuntimeError("int modulo by zero");
    if (left == std::numeric_limits<std::int64_t>::min() && right == -1) return 0;
    return left % right;
}

std::int64_t sun_int_neg(std::int64_t value) {
    if (value == std::numeric_limits<std::int64_t>::min())
        throw SunRuntimeError("int arithmetic overflow");
    return -value;
}

template <typename T>
T sun_min(T left, T right) {
    if constexpr (std::is_floating_point_v<T>) {
        if (std::isnan(left) || std::isnan(right)) {
            return std::numeric_limits<T>::quiet_NaN();
        }
    }
    return left <= right ? left : right;
}

template <typename T>
T sun_max(T left, T right) {
    if constexpr (std::is_floating_point_v<T>) {
        if (std::isnan(left) || std::isnan(right)) {
            return std::numeric_limits<T>::quiet_NaN();
        }
    }
    return left >= right ? left : right;
}

bool sun_all(const std::vector<bool>& values) {
    return std::all_of(values.begin(), values.end(), [](bool value) { return value; });
}

bool sun_any(const std::vector<bool>& values) {
    return std::any_of(values.begin(), values.end(), [](bool value) { return value; });
}

template <typename T>
decltype(auto) sun_list_at(std::vector<T>& values, double index) {
    if (!std::isfinite(index) || index < 0.0 || std::floor(index) != index ||
        index >= static_cast<double>(values.size()))
        throw SunRuntimeError("list index out of bounds or not a non-negative integer");
    return values[static_cast<std::size_t>(index)];
}

template <typename T>
void sun_list_append(std::vector<T>& values, T value) {
    values.push_back(std::move(value));
}

template <typename T>
bool sun_list_contains(const std::vector<T>& values, const T& value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

template <typename T>
bool sun_list_remove(std::vector<T>& values, const T& value) {
    const auto found = std::find(values.begin(), values.end(), value);
    if (found == values.end()) return false;
    values.erase(found);
    return true;
}

template <typename T>
void sun_list_insert(std::vector<T>& values, double index, T value) {
    if (!std::isfinite(index) || index < 0.0 || std::floor(index) != index ||
        index > static_cast<double>(values.size())) {
        throw SunRuntimeError("list insertion index out of bounds or not a non-negative integer");
    }
    values.insert(values.begin() + static_cast<std::ptrdiff_t>(index),
                  std::move(value));
}

template <typename T>
T sun_list_pop(std::vector<T>& values, double index) {
    if (!std::isfinite(index) || index < 0.0 || std::floor(index) != index ||
        index >= static_cast<double>(values.size())) {
        throw SunRuntimeError("list pop index out of bounds or not a non-negative integer");
    }
    const auto offset = static_cast<std::size_t>(index);
    T value = values[offset];
    values.erase(values.begin() + static_cast<std::ptrdiff_t>(offset));
    return value;
}

using SunBytes = std::vector<std::uint8_t>;

std::size_t sun_bytes_index(const SunBytes& values, double index) {
    if (!std::isfinite(index) || index < 0.0 || std::floor(index) != index ||
        index >= static_cast<double>(values.size())) {
        throw SunRuntimeError("bytes index out of bounds or not a non-negative integer");
    }
    return static_cast<std::size_t>(index);
}

std::uint8_t sun_bytes_value(double value) {
    if (!std::isfinite(value) || value < 0.0 || value > 255.0 ||
        std::floor(value) != value) {
        throw SunRuntimeError("byte value must be an integer from 0 to 255");
    }
    return static_cast<std::uint8_t>(value);
}

std::int64_t sun_bytes_at(const SunBytes& values, double index) {
    return static_cast<std::int64_t>(values[sun_bytes_index(values, index)]);
}

void sun_bytes_set(SunBytes& values, double index, double value) {
    values[sun_bytes_index(values, index)] = sun_bytes_value(value);
}

void sun_bytes_append(SunBytes& values, double value) {
    values.push_back(sun_bytes_value(value));
}

bool sun_bytes_contains(const SunBytes& values, double value) {
    const auto byte = sun_bytes_value(value);
    return std::find(values.begin(), values.end(), byte) != values.end();
}

SunBytes sun_bytes_from_string(const std::string& value) {
    SunBytes result;
    result.reserve(value.size());
    for (const auto byte : value)
        result.push_back(static_cast<std::uint8_t>(
            static_cast<unsigned char>(byte)));
    return result;
}

std::string sun_bytes_to_string(const SunBytes& value) {
    std::string result;
    result.reserve(value.size());
    for (const auto byte : value) result.push_back(static_cast<char>(byte));
    return result;
}

SunBytes sun_bytes_from_list(const std::vector<std::int64_t>& value) {
    SunBytes result;
    result.reserve(value.size());
    for (const auto byte : value) result.push_back(sun_bytes_value(static_cast<double>(byte)));
    return result;
}

std::vector<std::int64_t> sun_bytes_to_list(const SunBytes& value) {
    std::vector<std::int64_t> result;
    result.reserve(value.size());
    for (const auto byte : value) result.push_back(static_cast<std::int64_t>(byte));
    return result;
}
)cpp";
    if (program.io_imported) out << io_runtime_cpp();
    if (program.json_imported) out << json_runtime_cpp();
    if (program.memory_imported) {
        out << R"cpp(
class SunMemoryRegion {
public:
    static SunMemoryRegion allocate(double requested_size) {
        if (!std::isfinite(requested_size) || requested_size < 0.0 ||
            std::floor(requested_size) != requested_size ||
            requested_size > static_cast<double>(max_region_size)) {
            throw SunRuntimeError("memory.alloc size must be an integer from 0 to 67108864");
        }
        return SunMemoryRegion(
            std::make_shared<std::vector<std::uint8_t>>(
                static_cast<std::size_t>(requested_size), 0));
    }

    double size() const {
        return static_cast<double>(bytes_->size());
    }

    SunBytes to_bytes() const { return *bytes_; }

    static SunMemoryRegion from_bytes(const SunBytes& bytes) {
        if (bytes.size() > max_region_size) {
            throw SunRuntimeError("memory region is too large");
        }
        return SunMemoryRegion(
            std::make_shared<std::vector<std::uint8_t>>(bytes));
    }

    double read_u8(double offset) const {
        return static_cast<double>((*bytes_)[checked_offset(offset)]);
    }

    void write_u8(double offset, double value) {
        if (!std::isfinite(value) || value < 0.0 || value > 255.0 ||
            std::floor(value) != value) {
            throw SunRuntimeError("memory.write_u8 value must be an integer from 0 to 255");
        }
        (*bytes_)[checked_offset(offset)] = static_cast<std::uint8_t>(value);
    }

    std::int64_t read_u16(double offset, const std::string& endian) const {
        return static_cast<std::int64_t>(
            read_unsigned(offset, 2, endian, "memory.read_u16"));
    }

    std::int64_t read_u32(double offset, const std::string& endian) const {
        return static_cast<std::int64_t>(
            read_unsigned(offset, 4, endian, "memory.read_u32"));
    }

    void write_u16(double offset, double value, const std::string& endian) {
        write_unsigned(offset, value, 2, 65535.0, endian, "memory.write_u16");
    }

    void write_u32(double offset, double value, const std::string& endian) {
        write_unsigned(offset, value, 4, 4294967295.0, endian, "memory.write_u32");
    }

    void fill(double value) {
        if (!std::isfinite(value) || value < 0.0 || value > 255.0 ||
            std::floor(value) != value) {
            throw SunRuntimeError("memory.fill value must be an integer from 0 to 255");
        }
        std::fill(bytes_->begin(), bytes_->end(), static_cast<std::uint8_t>(value));
    }

    void copy_from(double destination_offset, const SunMemoryRegion& source,
                   double source_offset, double length) {
        const auto destination_range = checked_range(destination_offset, length);
        const auto source_range = source.checked_range(source_offset, length);
        const auto first = source.bytes_->begin() +
                           static_cast<std::ptrdiff_t>(source_range.first);
        const auto last = first + static_cast<std::ptrdiff_t>(source_range.second);
        const std::vector<std::uint8_t> copied(first, last);
        std::copy(copied.begin(), copied.end(),
                  bytes_->begin() +
                      static_cast<std::ptrdiff_t>(destination_range.first));
    }

private:
    static constexpr std::size_t max_region_size = 64 * 1024 * 1024;

    explicit SunMemoryRegion(std::shared_ptr<std::vector<std::uint8_t>> bytes)
        : bytes_(std::move(bytes)) {}

    static void validate_endian(const std::string& endian,
                                const std::string& operation) {
        if (endian != "little" && endian != "big") {
            throw SunRuntimeError(operation + " endian must be 'little' or 'big'");
        }
    }

    std::uint32_t read_unsigned(double offset, std::size_t width,
                                const std::string& endian,
                                const std::string& operation) const {
        validate_endian(endian, operation);
        const auto range = checked_range(offset, static_cast<double>(width));
        std::uint32_t result = 0;
        if (endian == "little") {
            for (std::size_t index = 0; index < width; ++index) {
                result |= static_cast<std::uint32_t>((*bytes_)[range.first + index])
                          << (index * 8);
            }
        } else {
            for (std::size_t index = 0; index < width; ++index) {
                result = (result << 8) | (*bytes_)[range.first + index];
            }
        }
        return result;
    }

    void write_unsigned(double offset, double value, std::size_t width,
                        double maximum, const std::string& endian,
                        const std::string& operation) {
        if (!std::isfinite(value) || value < 0.0 || value > maximum ||
            std::floor(value) != value) {
            throw SunRuntimeError(operation + " value must be an integer from 0 to " +
                                  std::to_string(static_cast<std::uint64_t>(maximum)));
        }
        validate_endian(endian, operation);
        const auto range = checked_range(offset, static_cast<double>(width));
        const auto encoded = static_cast<std::uint32_t>(value);
        for (std::size_t index = 0; index < width; ++index) {
            const auto shift = endian == "little"
                                   ? index * 8
                                   : (width - index - 1) * 8;
            (*bytes_)[range.first + index] =
                static_cast<std::uint8_t>((encoded >> shift) & 0xffU);
        }
    }

    std::size_t checked_offset(double offset) const {
        if (!std::isfinite(offset) || offset < 0.0 || std::floor(offset) != offset) {
            throw SunRuntimeError("memory offset must be a non-negative integer");
        }
        if (offset >= static_cast<double>(bytes_->size())) {
            throw SunRuntimeError("memory offset out of bounds");
        }
        return static_cast<std::size_t>(offset);
    }

    std::pair<std::size_t, std::size_t> checked_range(double offset,
                                                      double length) const {
        if (!std::isfinite(offset) || offset < 0.0 ||
            std::floor(offset) != offset) {
            throw SunRuntimeError("memory range offset must be a non-negative integer");
        }
        if (!std::isfinite(length) || length < 0.0 ||
            std::floor(length) != length) {
            throw SunRuntimeError("memory range length must be a non-negative integer");
        }
        if (offset > static_cast<double>(bytes_->size())) {
            throw SunRuntimeError("memory range out of bounds");
        }
        const auto begin = static_cast<std::size_t>(offset);
        const auto remaining = bytes_->size() - begin;
        if (length > static_cast<double>(remaining)) {
            throw SunRuntimeError("memory range out of bounds");
        }
        return {begin, static_cast<std::size_t>(length)};
    }

    std::shared_ptr<std::vector<std::uint8_t>> bytes_;
};

class SunMemoryPointer {
    struct Allocation {
        void* address = nullptr;
        std::size_t size = 0;
        ~Allocation() { std::free(address); }
    };

public:
    SunMemoryPointer() = default;

    static SunMemoryPointer allocate(double requested_size) {
        if (!std::isfinite(requested_size) || requested_size < 0.0 ||
            std::floor(requested_size) != requested_size ||
            requested_size > static_cast<double>(64 * 1024 * 1024)) {
            throw SunRuntimeError("memory.alloc_ptr size must be an integer from 0 to 67108864");
        }
        const auto size = static_cast<std::size_t>(requested_size);
        if (size == 0) return {};
        auto allocation = std::make_shared<Allocation>();
        allocation->address = std::malloc(size);
        if (!allocation->address) {
            throw SunRuntimeError("memory.alloc_ptr could not allocate memory");
        }
        allocation->size = size;
        return SunMemoryPointer(std::move(allocation));
    }

    void* get() const {
        return allocation_ ? allocation_->address : nullptr;
    }

    bool is_null() const { return get() == nullptr; }

    std::int64_t size() const {
        return allocation_ && allocation_->address
                   ? static_cast<std::int64_t>(allocation_->size)
                   : 0;
    }

    void release() const {
        if (!allocation_ || !allocation_->address) return;
        std::free(allocation_->address);
        allocation_->address = nullptr;
        allocation_->size = 0;
    }

private:
    explicit SunMemoryPointer(std::shared_ptr<Allocation> allocation)
        : allocation_(std::move(allocation)) {}

    std::shared_ptr<Allocation> allocation_;
};
)cpp";
    }
    for (const auto& type : program.custom_types) {
        if (type.is_error_base) continue;
        if (type.is_enum) {
            const bool has_payload = std::any_of(
                type.enum_variants.begin(), type.enum_variants.end(),
                [](const EnumVariant& variant) { return !variant.fields.empty(); });
            if (!has_payload) {
                out << "\nenum class SunType_" << type.name << " { ";
                for (std::size_t i = 0; i < type.enum_values.size(); ++i) {
                    if (i) out << ", ";
                    out << type.enum_values[i];
                }
                out << " };\n";
                continue;
            }
            out << '\n';
            for (const auto& variant : type.enum_variants) {
                out << "struct SunType_" << type.name << '_' << variant.name << " {\n";
                for (const auto& field : variant.fields) {
                    out << "    " << cpp_type(field.type) << ' ' << field.name << "{};\n";
                }
                out << "    friend bool operator==(const SunType_" << type.name << '_'
                    << variant.name << "& left, const SunType_" << type.name << '_'
                    << variant.name << "& right) { return ";
                if (variant.fields.empty()) {
                    out << "true";
                } else {
                    for (std::size_t i = 0; i < variant.fields.size(); ++i) {
                        if (i) out << " && ";
                        out << "left." << variant.fields[i].name << " == right."
                            << variant.fields[i].name;
                    }
                }
                out << "; }\n"
                    << "    friend bool operator!=(const SunType_" << type.name << '_'
                    << variant.name << "& left, const SunType_" << type.name << '_'
                    << variant.name << "& right) { return !(left == right); }\n"
                    << "};\n";
            }
            out << "using SunType_" << type.name << " = std::variant<";
            for (std::size_t i = 0; i < type.enum_variants.size(); ++i) {
                if (i) out << ", ";
                out << "SunType_" << type.name << '_' << type.enum_variants[i].name;
            }
            out << ">;\n";
            continue;
        }
        out << "\nstruct SunType_" << type.name;
        if (type.is_error) {
            out << " : public SunRuntimeError";
        } else if (!type.base_type.empty()) {
            out << " : public SunType_" << type.base_type;
        }
        out << " {\n";
        if (type.is_error) {
            const auto init = std::find_if(
                type.methods.begin(), type.methods.end(),
                [](const Function& method) { return method.name == "init"; });
            if (init == type.methods.end()) {
                out << "    using SunRuntimeError::SunRuntimeError;\n";
            } else {
                out << "    SunType_" << type.name << '(';
                for (std::size_t i = 0; i < init->parameters.size(); ++i) {
                    if (i) out << ", ";
                    out << cpp_type(init->parameters[i].type) << " sun_arg_"
                        << init->parameters[i].name;
                }
                out << ");\n";
            }
        }
        for (const auto& field : type.fields) {
            out << "    " << cpp_type(field.type) << ' ' << field.name << "{};\n";
        }
        for (const auto& method : type.methods) {
            const bool inherited = method.name != "init" &&
                                   inherits_method(program, type, method.name);
            out << "    ";
            if (method.name != "init") out << "virtual ";
            out << (method.return_type ? cpp_type(*method.return_type) : "void")
                << ' ' << method.name << '(';
            for (std::size_t i = 0; i < method.parameters.size(); ++i) {
                if (i) out << ", ";
                out << cpp_type(method.parameters[i].type);
            }
            out << ')';
            if (inherited) out << " override";
            out << ";\n";
        }
        out << "};\n";
    }
    for (const auto& type : program.custom_types) {
        const auto [initializer_owner, initializer] = class_initializer(program, type);
        (void)initializer_owner;
        if (!type.is_class || type.is_error ||
            (!initializer && type.base_type.empty())) continue;
        out << "\nSunType_" << type.name << " sun_make_" << type.name << '(';
        if (initializer) {
            for (std::size_t i = 0; i < initializer->parameters.size(); ++i) {
                if (i) out << ", ";
                out << cpp_type(initializer->parameters[i].type);
            }
        } else {
            const auto fields = all_class_fields(program, type);
            for (std::size_t i = 0; i < fields.size(); ++i) {
                if (i) out << ", ";
                out << cpp_type(fields[i].type);
            }
        }
        out << ");\n";
    }
    for (const auto& code : program.global_cpp) {
        out << '\n' << code << '\n';
    }
    out << "\nstd::string sun_to_string(const std::string& value) {\n"
           "    return value;\n"
           "}\n\n"
           "std::string sun_to_string(const SunBytes& value) {\n"
           "    std::ostringstream stream;\n"
           "    stream << '[';\n"
           "    for (std::size_t i = 0; i < value.size(); ++i) {\n"
           "        if (i) stream << \", \";\n"
           "        stream << static_cast<unsigned int>(value[i]);\n"
           "    }\n"
           "    stream << ']';\n"
           "    return stream.str();\n"
           "}\n\n"
           "template <typename T>\n"
           "std::string sun_to_string(const T& value) {\n"
           "    std::ostringstream stream;\n"
           "    stream << std::boolalpha << value;\n"
           "    return stream.str();\n"
           "}\n\n"
           "template <typename Tuple, std::size_t... Indices>\n"
           "std::string sun_tuple_to_string_impl(const Tuple& value, "
           "std::index_sequence<Indices...>) {\n"
           "    std::ostringstream stream;\n"
           "    stream << '(';\n"
           "    std::size_t index = 0;\n"
           "    ((stream << (index++ ? \", \" : \"\") << "
           "sun_to_string(std::get<Indices>(value))), ...);\n"
           "    if constexpr (sizeof...(Indices) == 1) stream << ',';\n"
           "    stream << ')';\n"
           "    return stream.str();\n"
           "}\n\n"
           "template <typename... Types>\n"
           "std::string sun_to_string(const std::tuple<Types...>& value) {\n"
           "    return sun_tuple_to_string_impl(value, std::index_sequence_for<Types...>{});\n"
           "}\n\n"
           "template <typename Tuple, typename Value, std::size_t... Indices>\n"
           "std::int64_t sun_tuple_count_impl(const Tuple& tuple, const Value& value, "
           "std::index_sequence<Indices...>) {\n"
           "    const auto count_one = [&value](const auto& item) -> std::int64_t {\n"
           "        using Item = std::decay_t<decltype(item)>;\n"
           "        if constexpr (std::is_same_v<Item, std::decay_t<Value>>) "
           "return item == value ? 1 : 0;\n"
           "        else return 0;\n"
           "    };\n"
           "    return (std::int64_t{0} + ... + count_one(std::get<Indices>(tuple)));\n"
           "}\n\n"
           "template <typename... Types, typename Value>\n"
           "std::int64_t sun_tuple_count(const std::tuple<Types...>& tuple, "
           "const Value& value) {\n"
           "    return sun_tuple_count_impl(tuple, value, "
           "std::index_sequence_for<Types...>{});\n"
           "}\n\n"
           "template <typename Tuple, typename Value, std::size_t... Indices>\n"
           "std::int64_t sun_tuple_index_impl(const Tuple& tuple, const Value& value, "
           "std::index_sequence<Indices...>) {\n"
           "    std::int64_t found = -1;\n"
           "    (([&] {\n"
           "        using Item = std::decay_t<decltype(std::get<Indices>(tuple))>;\n"
           "        if constexpr (std::is_same_v<Item, std::decay_t<Value>>) {\n"
           "            if (found < 0 && std::get<Indices>(tuple) == value) "
           "found = static_cast<std::int64_t>(Indices);\n"
           "        }\n"
           "    }()), ...);\n"
           "    if (found < 0) throw std::out_of_range(\"tuple.index: value not found\");\n"
           "    return found;\n"
           "}\n\n"
           "template <typename... Types, typename Value>\n"
           "std::int64_t sun_tuple_index(const std::tuple<Types...>& tuple, "
           "const Value& value) {\n"
           "    return sun_tuple_index_impl(tuple, value, "
           "std::index_sequence_for<Types...>{});\n"
           "}\n\n"
           "template <typename T>\n"
           "std::string sun_to_string(const std::vector<T>& values) {\n"
           "    std::ostringstream stream;\n"
           "    stream << '[';\n"
           "    for (std::size_t i = 0; i < values.size(); ++i) {\n"
           "        if (i) stream << \", \";\n"
           "        stream << std::boolalpha << values[i];\n"
           "    }\n"
           "    stream << ']';\n"
           "    return stream.str();\n"
           "}\n\n"
           "template <typename K, typename V>\n"
           "std::string sun_to_string(const std::map<K, V>& values) {\n"
           "    std::ostringstream stream;\n"
           "    stream << '{';\n"
           "    std::size_t index = 0;\n"
           "    for (const auto& entry : values) {\n"
           "        if (index++) stream << \", \";\n"
           "        stream << std::boolalpha << entry.first << \": \" << entry.second;\n"
           "    }\n"
           "    stream << '}';\n"
           "    return stream.str();\n"
           "}\n\n"
           "double sun_range_next(double value, double step) {\n"
           "    const double next = value + step;\n"
           "    if (next == value) {\n"
           "        throw SunRuntimeError(\"range step does not advance number\");\n"
           "    }\n"
           "    return next;\n"
           "}\n\n";
    if (program.memory_imported) {
        out << "std::string sun_to_string(const SunMemoryRegion& value) {\n"
               "    return \"region(size=\" + sun_to_string(value.size()) + \")\";\n"
               "}\n\n";
    }
    for (const auto& type : program.custom_types) {
        if (!type.is_enum) continue;
        const bool has_payload = std::any_of(
            type.enum_variants.begin(), type.enum_variants.end(),
            [](const EnumVariant& variant) { return !variant.fields.empty(); });
        if (has_payload) {
            out << "std::string sun_to_string(const SunType_" << type.name
                << "& value) {\n"
                << "    return std::visit([](const auto& variant) -> std::string {\n";
            for (std::size_t i = 0; i < type.enum_variants.size(); ++i) {
                const auto& variant = type.enum_variants[i];
                out << (i ? "    else " : "    ")
                    << "if constexpr (std::is_same_v<std::decay_t<decltype(variant)>, "
                    << "SunType_" << type.name << '_' << variant.name << ">) return \""
                    << variant.name << "\";\n";
            }
            out << "    }, value);\n}\n\n";
            continue;
        }
        out << "std::string sun_to_string(SunType_" << type.name << " value) {\n"
            << "    switch (value) {\n";
        for (const auto& value : type.enum_values) {
            out << "    case SunType_" << type.name << "::" << value << ": return "
                << '"' << value << '"' << ";\n";
        }
        out << "    }\n    return \"<invalid " << type.name << ">\";\n}\n\n";
    }
    for (const auto& type : program.custom_types) {
        if (type.is_enum || type.is_error || type.is_error_base) continue;
        const auto fields = type.is_class ? all_class_fields(program, type) : type.fields;
        out << "std::string sun_to_string(const SunType_" << type.name
            << "& value) {\n"
            << "    std::ostringstream stream;\n"
            << "    stream << \"" << type.name << "{\";\n";
        for (std::size_t i = 0; i < fields.size(); ++i) {
            if (i) out << "    stream << \", \";\n";
            out << "    stream << \"" << fields[i].name << "=\" << "
                << "sun_to_string(value." << fields[i].name << ");\n";
        }
        out << "    stream << \"}\";\n"
            << "    return stream.str();\n"
            << "}\n\n";
    }
    for (const auto& function : program.functions) {
        if (!function.owner_type.empty()) continue;
        out << (function.return_type ? cpp_type(*function.return_type) : "void")
            << " sun_fn_" << function.name << '(';
        for (std::size_t i = 0; i < function.parameters.size(); ++i) {
            if (i) out << ", ";
            out << cpp_type(function.parameters[i].type);
        }
        out << ");\n";
    }
    if (!program.functions.empty()) out << '\n';
    for (const auto& function : program.functions) {
        if (function.owner_type.empty()) continue;
        out << (function.return_type ? cpp_type(*function.return_type) : "void")
            << " SunType_" << function.owner_type << "::" << function.name << '(';
        for (std::size_t i = 0; i < function.parameters.size(); ++i) {
            if (i) out << ", ";
            out << cpp_type(function.parameters[i].type) << " sun_arg_"
                << function.parameters[i].name;
        }
        out << ") {\n" << render_block(function.body, 4) << "}\n\n";
    }
    for (const auto& type : program.custom_types) {
        if (!type.is_error || type.is_error_base) continue;
        const auto init = std::find_if(
            type.methods.begin(), type.methods.end(),
            [](const Function& method) { return method.name == "init"; });
        if (init == type.methods.end()) continue;
        out << "SunType_" << type.name << "::SunType_" << type.name << '(';
        for (std::size_t i = 0; i < init->parameters.size(); ++i) {
            if (i) out << ", ";
            out << cpp_type(init->parameters[i].type) << " sun_arg_"
                << init->parameters[i].name;
        }
        out << ") : SunRuntimeError(sun_arg_" << init->parameters.front().name << ") {\n"
            << "    init(";
        for (std::size_t i = 0; i < init->parameters.size(); ++i) {
            if (i) out << ", ";
            out << "sun_arg_" << init->parameters[i].name;
        }
        out << ");\n}\n\n";
    }
    for (const auto& type : program.custom_types) {
        const auto [initializer_owner, initializer] = class_initializer(program, type);
        (void)initializer_owner;
        if (!type.is_class || type.is_error ||
            (!initializer && type.base_type.empty())) continue;
        out << "SunType_" << type.name << " sun_make_" << type.name << '(';
        if (initializer) {
            for (std::size_t i = 0; i < initializer->parameters.size(); ++i) {
                if (i) out << ", ";
                out << cpp_type(initializer->parameters[i].type) << " sun_arg_"
                    << initializer->parameters[i].name;
            }
        } else {
            const auto fields = all_class_fields(program, type);
            for (std::size_t i = 0; i < fields.size(); ++i) {
                if (i) out << ", ";
                out << cpp_type(fields[i].type) << " sun_arg_" << fields[i].name;
            }
        }
        out << ") {\n    SunType_" << type.name << " sun_instance{};\n";
        if (initializer) {
            out << "    sun_instance.init(";
            for (std::size_t i = 0; i < initializer->parameters.size(); ++i) {
                if (i) out << ", ";
                out << "sun_arg_" << initializer->parameters[i].name;
            }
            out << ");\n";
        } else {
            for (const auto& field : all_class_fields(program, type)) {
                out << "    sun_instance." << field.name << " = sun_arg_"
                    << field.name << ";\n";
            }
        }
        out << "    return sun_instance;\n}\n\n";
    }
    for (const auto& function : program.functions) {
        if (!function.owner_type.empty()) continue;
        out << (function.return_type ? cpp_type(*function.return_type) : "void")
            << " sun_fn_" << function.name << '(';
        for (std::size_t i = 0; i < function.parameters.size(); ++i) {
            if (i) out << ", ";
            out << cpp_type(function.parameters[i].type) << " sun_arg_"
                << function.parameters[i].name;
        }
        out << ") {\n" << render_block(function.body, 4) << "}\n\n";
    }
    if (program.sys_imported) {
        out << "int main(int argc, char** argv) {\n";
    } else {
        out << "int main() {\n";
    }
    out << "    try {\n";
    if (program.sys_imported) out << "        sun_sys_initialize(argc, argv);\n";
    out << render_block(program.main_body, 8)
        << "        return 0;\n"
        << "    } catch (const std::exception& error) {\n"
        << "        std::cerr << \"Sun runtime error: \" << error.what() << '\\n';\n"
        << "        return 1;\n"
        << "    }\n}\n";
    return out.str();
}

} // namespace sun
