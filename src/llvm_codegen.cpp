#include "compiler.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iomanip>
#include <limits>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace sun {
namespace {

struct Value {
    Type type;
    std::string operand;
    std::string string_length = "0";
};

bool is_string_like(Type type) {
    return type == Type::string || type == Type::character;
}

bool is_dictionary_type(Type type) {
    return type.kind == Type::Kind::dictionary;
}

bool is_list_type(Type type) {
    return type.kind == Type::Kind::list_number ||
           type.kind == Type::Kind::list_integer ||
           type.kind == Type::Kind::list_string ||
           type.kind == Type::Kind::list_boolean ||
           type.kind == Type::Kind::list_character ||
           type.kind == Type::Kind::list_complex;
}

Type llvm_decode_type(const std::string& source);
Type llvm_list_type(Type item);

Type list_item_type(Type type) {
    if (type.kind == Type::Kind::list_number) return Type::number;
    if (type.kind == Type::Kind::list_integer) return Type::integer;
    if (type.kind == Type::Kind::list_string) return Type::string;
    if (type.kind == Type::Kind::list_boolean) return Type::boolean;
    if (type.kind == Type::Kind::list_character) return Type::character;
    if (type.kind == Type::Kind::list_complex) {
        return llvm_decode_type(type.name);
    }
    throw std::logic_error("not a Sun list type");
}

std::vector<std::string> split_llvm_types(const std::string& source) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    int depth = 0;
    for (std::size_t i = 0; i < source.size(); ++i) {
        if (source[i] == '[') ++depth;
        else if (source[i] == ']') --depth;
        else if (source[i] == ',' && depth == 0) {
            parts.push_back(source.substr(start, i - start));
            start = i + 1;
        }
    }
    parts.push_back(source.substr(start));
    return parts;
}

Type llvm_decode_type(const std::string& source) {
    const auto name = source;
    if (name == "int") return Type::integer;
    if (name == "number") return Type::number;
    if (name == "bool") return Type::boolean;
    if (name == "string" || name == "str") return Type::string;
    if (name == "char") return Type::character;
    if (name.rfind("list[", 0) == 0 && name.back() == ']') {
        return llvm_list_type(llvm_decode_type(name.substr(5, name.size() - 6)));
    }
    if (name.rfind("dict[", 0) == 0 && name.back() == ']') {
        const auto inner = name.substr(5, name.size() - 6);
        const auto parts = split_llvm_types(inner);
        if (parts.size() != 2) throw std::logic_error("invalid LLVM dictionary type");
        return {Type::Kind::dictionary,
                parts[0] + "," + parts[1]};
    }
    if (name.rfind("tuple[", 0) == 0 && name.back() == ']') {
        return {Type::Kind::tuple, name.substr(6, name.size() - 7)};
    }
    return {Type::Kind::custom, name};
}

Type dictionary_key_type(Type type) {
    const auto parts = split_llvm_types(type.name);
    if (parts.size() != 2) throw std::logic_error("invalid dictionary type");
    return llvm_decode_type(parts.front());
}

Type dictionary_value_type(Type type) {
    const auto parts = split_llvm_types(type.name);
    if (parts.size() != 2) throw std::logic_error("invalid dictionary type");
    return llvm_decode_type(parts.back());
}

std::vector<Type> llvm_tuple_types(Type type) {
    if (type.kind != Type::Kind::tuple) {
        throw std::logic_error("type is not an LLVM tuple");
    }
    std::vector<Type> items;
    for (const auto& encoded : split_llvm_types(type.name)) {
        items.push_back(llvm_decode_type(encoded));
    }
    return items;
}

Type llvm_list_type(Type item) {
    if (item == Type::integer) return Type::list_integer;
    if (item == Type::number) return Type::list_number;
    if (item == Type::boolean) return Type::list_boolean;
    if (item == Type::string) return Type::list_string;
    if (item == Type::character) return Type::list_character;
    return {Type::Kind::list_complex,
            item.kind == Type::Kind::custom ? item.name
            : item.kind == Type::Kind::dictionary ? "dict[" + item.name + "]"
            : item.kind == Type::Kind::tuple ? "tuple[" + item.name + "]"
            : item.kind == Type::Kind::list_complex ? "list[" + item.name + "]"
            : item.kind == Type::Kind::list_integer ? "list[int]"
            : item.kind == Type::Kind::list_number ? "list[number]"
            : item.kind == Type::Kind::list_string ? "list[string]"
            : item.kind == Type::Kind::list_boolean ? "list[bool]"
            : item.kind == Type::Kind::list_character ? "list[char]"
            : cpp_type(item)};
}

std::string llvm_list_suffix(Type type) {
    if (type == Type::integer) return "i64";
    if (type == Type::number) return "double";
    if (type == Type::boolean) return "bool";
    if (type == Type::string || type == Type::character) return "string";
    if (type.kind == Type::Kind::custom || is_list_type(type) ||
        is_dictionary_type(type) || type.kind == Type::Kind::tuple) return "ptr";
    throw std::logic_error("unsupported LLVM list item type '" + cpp_type(type) + "'");
}

std::string llvm_type(Type type) {
    if (is_list_type(type) || type.kind == Type::Kind::empty_list ||
        is_dictionary_type(type) || type.kind == Type::Kind::empty_dictionary ||
        type.kind == Type::Kind::custom) return "ptr";
    if (type.kind == Type::Kind::tuple) {
        std::ostringstream result;
        result << "{ ";
        const auto items = llvm_tuple_types(type);
        for (std::size_t i = 0; i < items.size(); ++i) {
            if (i) result << ", ";
            if (is_string_like(items[i])) result << "{ ptr, i64 }";
            else result << llvm_type(items[i]);
        }
        result << " }";
        return result.str();
    }
    if (type == Type::integer) return "i64";
    if (type == Type::number) return "double";
    if (type == Type::boolean) return "i1";
    if (type == Type::bytes) return "ptr";
    if (is_string_like(type)) return "ptr";
    throw std::logic_error("LLVM backend does not support type '" +
                           cpp_type(type) + "'");
}

std::string llvm_string(const std::string& value) {
    std::ostringstream out;
    for (unsigned char byte : value) {
        if (byte >= 32 && byte < 127 && byte != '\\' && byte != '"') {
            out << static_cast<char>(byte);
        } else {
            out << '\\' << std::hex << std::uppercase
                << std::setw(2) << std::setfill('0')
                << static_cast<unsigned>(byte) << std::dec;
        }
    }
    return out.str();
}

std::string llvm_double(double value) {
    std::uint64_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value), "unexpected double size");
    std::memcpy(&bits, &value, sizeof(bits));
    std::ostringstream out;
    out << "0x" << std::uppercase << std::hex << std::setw(16)
        << std::setfill('0') << bits;
    return out.str();
}

class Generator {
    struct LocalFunctionInfo {
        Function function;
        std::vector<std::pair<std::string, Type>> captures;
    };

public:
    std::string generate(const Program& program) {
        for (const auto& type : program.custom_types) {
            custom_types_.emplace(type.name, &type);
        }
        for (const auto& type : program.custom_types) {
            if (!type.is_error) continue;
            custom_error_types_.insert(type.name);
            if (!type.fields.empty()) {
                unsupported("custom error fields in LLVM");
            }
            for (const auto& method : type.methods) {
                if (method.name != "init") {
                    unsupported("custom error methods in LLVM");
                }
                if (method.parameters.size() != 1 || !method.body.empty()) {
                    unsupported("custom error initializers beyond a message in LLVM");
                }
            }
        }
        for (const auto& function : program.functions) {
            if (!function.owner_type.empty() &&
                custom_error_types_.count(function.owner_type)) {
                if (function.name == "init") continue;
                unsupported("custom error methods in LLVM");
            }
            if (function.owner_type.empty()) {
                functions_.emplace("sun_fn_" + function.name, &function);
            } else {
                methods_.emplace(function.owner_type + "." + function.name,
                                 &function);
            }
        }
        std::vector<std::string> function_ir;
        for (const auto& function : program.functions) {
            if (function.owner_type.empty() ||
                !custom_error_types_.count(function.owner_type)) {
                function_ir.push_back(emit_function(function));
            }
        }

        reset_function_state();
        predeclare_block(program.main_body);
        prepare_local_functions(program.main_body, variables_);
        emit_allocas();
        emit_block(program.main_body);
        if (!terminated_) out_ << "  ret i32 0\n";
        const auto main_ir = out_.str();
        for (const auto& name : local_function_order_) {
            function_ir.push_back(emit_function(local_functions_.at(name).function));
        }

        std::ostringstream module;
        for (const auto& type : program.custom_types) {
            if (type.is_error) continue;
            if (type.is_enum) {
                std::size_t payload_slots = 0;
                for (const auto& variant : type.enum_variants) {
                    payload_slots = std::max(payload_slots,
                                              variant.fields.size() * 2);
                }
                module << "%sun.type." << type.name
                       << " = type { i32, [" << payload_slots << " x i64] }\n";
                continue;
            }
            const auto fields = custom_fields(type.name);
            module << "%sun.type." << type.name << " = type { ";
            for (std::size_t i = 0; i < fields.size(); ++i) {
                if (i) module << ", ";
                module << (is_string_like(fields[i].type)
                               ? "{ ptr, i64 }"
                               : llvm_type(fields[i].type));
            }
            module << " }\n";
        }
        if (!program.custom_types.empty()) module << '\n';
        for (const auto& [name, value] : strings_) {
            module << name << " = private unnamed_addr constant ["
                   << value.size() + 1 << " x i8] c\""
                   << llvm_string(value) << "\\00\"\n";
        }
        module << "@.sun.fmt.int = private unnamed_addr constant [6 x i8] c\"%lld\\0A\\00\"\n"
                  "@.sun.fmt.int.no_newline = private unnamed_addr constant [5 x i8] c\"%lld\\00\"\n"
                  "@.sun.fmt.number = private unnamed_addr constant [4 x i8] c\"%g\\0A\\00\"\n"
                  "@.sun.fmt.number.no_newline = private unnamed_addr constant [3 x i8] c\"%g\\00\"\n"
                  "@.sun.fmt.string = private unnamed_addr constant [3 x i8] c\"%s\\00\"\n"
                  "@.sun.fmt.comma = private unnamed_addr constant [3 x i8] c\", \\00\"\n"
                  "@.sun.bool.true = private unnamed_addr constant [5 x i8] c\"true\\00\"\n"
                  "@.sun.bool.false = private unnamed_addr constant [6 x i8] c\"false\\00\"\n"
                  "@.sun.fmt.int.raw = private unnamed_addr constant [5 x i8] c\"%lld\\00\"\n"
                  "@.sun.fmt.number.raw = private unnamed_addr constant [3 x i8] c\"%g\\00\"\n"
                  "@stdout = external global ptr\n\n"
                  "declare i32 @printf(ptr, ...)\n"
                  "declare i32 @puts(ptr)\n"
                  "declare i64 @fwrite(ptr, i64, i64, ptr)\n"
                  "declare i32 @memcmp(ptr, ptr, i64)\n"
                  "declare ptr @malloc(i64)\n"
                  "declare i32 @snprintf(ptr, i64, ptr, ...)\n"
                  "declare void @llvm.memcpy.p0.p0.i64(ptr, ptr, i64, i1)\n"
                  "declare double @llvm.fabs.f64(double)\n"
                  "declare double @llvm.floor.f64(double)\n"
                  "declare double @llvm.ceil.f64(double)\n"
                  "declare double @llvm.trunc.f64(double)\n"
                  "declare double @llvm.round.f64(double)\n"
                  "declare i32 @putchar(i32)\n"
                  "declare i32 @__gxx_personality_v0(...)\n"
                  "declare i32 @llvm.eh.typeid.for(ptr)\n"
                  "declare ptr @sun_llvm_catch_exception(ptr, ptr)\n"
                  "declare i1 @sun_llvm_exception_matches(ptr, ptr, i64)\n"
                  "declare ptr @sun_llvm_error_create(ptr, i64, ptr, i64)\n"
                  "declare void @sun_llvm_raise_custom(ptr)\n"
                  "declare void @sun_llvm_rethrow_exception(ptr)\n"
                  "@_ZTISt9exception = external constant ptr\n"
                  "declare void @sun_llvm_tuple_index_missing()\n"
                  "declare i64 @sun_llvm_checked_add(i64, i64)\n"
                  "declare i64 @sun_llvm_checked_sub(i64, i64)\n"
                  "declare i64 @sun_llvm_checked_mul(i64, i64)\n"
                  "declare i64 @sun_llvm_checked_div(i64, i64)\n"
                  "declare i64 @sun_llvm_checked_mod(i64, i64)\n"
                  "declare i64 @sun_llvm_checked_neg(i64)\n"
                  "declare i64 @sun_llvm_checked_number_to_int(double)\n"
                  "declare ptr @sun_llvm_checked_char(ptr, i64)\n"
                  "declare void @sun_llvm_range_step_zero()\n"
                  "declare void @sun_llvm_assert(i1)\n"
                  "declare void @sun_llvm_raise(ptr, i64)\n"
                  "declare ptr @sun_llvm_bytes_create()\n"
                  "declare ptr @sun_llvm_bytes_from_list(ptr)\n"
                  "declare ptr @sun_llvm_bytes_to_list(ptr)\n"
                  "declare i64 @sun_llvm_bytes_len(ptr)\n"
                  "declare i64 @sun_llvm_bytes_at(ptr, double)\n"
                  "declare void @sun_llvm_bytes_set(ptr, double, i64)\n"
                  "declare void @sun_llvm_bytes_append(ptr, i64)\n"
                  "declare i1 @sun_llvm_bytes_contains(ptr, i64)\n"
                  "declare void @sun_llvm_bytes_clear(ptr)\n"
                  "declare ptr @sun_llvm_bytes_from_string(ptr, i64)\n"
                  "declare ptr @sun_llvm_bytes_to_string(ptr, ptr)\n"
                  "declare ptr @sun_llvm_string_concat(ptr, i64, ptr, i64, ptr)\n"
                  "declare ptr @sun_llvm_string_upper(ptr, i64, ptr)\n"
                  "declare ptr @sun_llvm_string_lower(ptr, i64, ptr)\n"
                  "declare ptr @sun_llvm_string_trim(ptr, i64, ptr)\n"
                  "declare ptr @sun_llvm_string_split(ptr, i64, ptr, i64)\n"
                  "declare ptr @sun_llvm_string_replace(ptr, i64, ptr, i64, ptr, i64, ptr)\n"
                  "declare i1 @sun_llvm_string_contains(ptr, i64, ptr, i64)\n"
                  "declare i64 @sun_llvm_string_count(ptr, i64, ptr, i64)\n"
                  "declare i1 @sun_llvm_string_starts_with(ptr, i64, ptr, i64)\n"
                  "declare i1 @sun_llvm_string_ends_with(ptr, i64, ptr, i64)\n"
                  "declare i64 @sun_llvm_list_sum_i64(ptr)\n"
                  "declare double @sun_llvm_list_sum_double(ptr)\n"
                  "declare i1 @sun_llvm_list_all_bool(ptr)\n"
                  "declare i1 @sun_llvm_list_any_bool(ptr)\n"
                  "declare double @sun_llvm_round_digits(double, i64)\n"
                  "declare double @sun_llvm_math_factorial(double)\n"
                  "declare double @sun_llvm_math_acos(double)\n"
                  "declare double @sun_llvm_math_asin(double)\n"
                  "declare double @sun_llvm_math_atan(double)\n"
                  "declare double @sun_llvm_math_atan2(double, double)\n"
                  "declare double @sun_llvm_math_cos(double)\n"
                  "declare double @sun_llvm_math_exp(double)\n"
                  "declare double @sun_llvm_math_fmod(double, double)\n"
                  "declare double @sun_llvm_math_hypot(double, double)\n"
                  "declare double @sun_llvm_math_log(double)\n"
                  "declare double @sun_llvm_math_log10(double)\n"
                  "declare double @sun_llvm_math_log2(double)\n"
                  "declare double @sun_llvm_math_pow(double, double)\n"
                  "declare double @sun_llvm_math_sin(double)\n"
                  "declare double @sun_llvm_math_sqrt(double)\n"
                  "declare double @sun_llvm_math_tan(double)\n"
                  "declare ptr @sun_llvm_input_string(ptr, i64, ptr)\n"
                  "declare double @sun_llvm_input_number(ptr, i64)\n"
                  "declare i64 @sun_llvm_input_int(ptr, i64)\n"
                  "declare i1 @sun_llvm_input_bool(ptr, i64)\n"
                  "declare ptr @sun_llvm_input_char(ptr, i64, ptr)\n"
                  "declare i64 @sun_llvm_io_open(ptr, i64, ptr, i64)\n"
                  "declare void @sun_llvm_io_write(i64, ptr, i64)\n"
                  "declare ptr @sun_llvm_io_read(i64, ptr)\n"
                  "declare void @sun_llvm_io_close(i64)\n"
                  "declare void @sun_llvm_io_close_noexcept(i64)\n"
                  "declare ptr @sun_llvm_dict_create()\n"
                  "declare ptr @sun_llvm_dict_to_string(ptr, ptr)\n"
                  "declare i64 @sun_llvm_dict_len(ptr)\n"
                  "declare ptr @sun_llvm_dict_key_at(ptr, double, ptr)\n"
                  "declare ptr @sun_llvm_dict_value_ptr_at(ptr, double)\n"
                  "declare void @sun_llvm_dict_clear(ptr)\n"
                  "declare i64 @sun_llvm_dict_at_i64_i64(ptr, i64)\n"
                  "declare double @sun_llvm_dict_at_i64_double(ptr, i64)\n"
                  "declare i1 @sun_llvm_dict_at_i64_bool(ptr, i64)\n"
                  "declare ptr @sun_llvm_dict_at_i64_string(ptr, i64, ptr)\n"
                  "declare i64 @sun_llvm_dict_at_string_i64(ptr, ptr, i64)\n"
                  "declare double @sun_llvm_dict_at_string_double(ptr, ptr, i64)\n"
                  "declare i1 @sun_llvm_dict_at_string_bool(ptr, ptr, i64)\n"
                  "declare ptr @sun_llvm_dict_at_string_string(ptr, ptr, i64, ptr)\n"
                  "declare i64 @sun_llvm_dict_at_bool_i64(ptr, i1)\n"
                  "declare double @sun_llvm_dict_at_bool_double(ptr, i1)\n"
                  "declare i1 @sun_llvm_dict_at_bool_bool(ptr, i1)\n"
                  "declare ptr @sun_llvm_dict_at_bool_string(ptr, i1, ptr)\n"
                  "declare ptr @sun_llvm_dict_at_i64_ptr(ptr, i64)\n"
                  "declare ptr @sun_llvm_dict_at_string_ptr(ptr, ptr, i64)\n"
                  "declare ptr @sun_llvm_dict_at_bool_ptr(ptr, i1)\n"
                  "declare i1 @sun_llvm_dict_contains_i64(ptr, i64)\n"
                  "declare i1 @sun_llvm_dict_contains_string(ptr, ptr, i64)\n"
                  "declare i1 @sun_llvm_dict_contains_bool(ptr, i1)\n"
                  "declare i1 @sun_llvm_dict_remove_i64(ptr, i64)\n"
                  "declare i1 @sun_llvm_dict_remove_string(ptr, ptr, i64)\n"
                  "declare i1 @sun_llvm_dict_remove_bool(ptr, i1)\n"
                  "declare i64 @sun_llvm_dict_get_i64_i64(ptr, i64, i64)\n"
                  "declare double @sun_llvm_dict_get_i64_double(ptr, i64, double)\n"
                  "declare i1 @sun_llvm_dict_get_i64_bool(ptr, i64, i1)\n"
                  "declare ptr @sun_llvm_dict_get_i64_string(ptr, i64, ptr, i64, ptr)\n"
                  "declare i64 @sun_llvm_dict_get_string_i64(ptr, ptr, i64, i64)\n"
                  "declare double @sun_llvm_dict_get_string_double(ptr, ptr, i64, double)\n"
                  "declare i1 @sun_llvm_dict_get_string_bool(ptr, ptr, i64, i1)\n"
                  "declare ptr @sun_llvm_dict_get_string_string(ptr, ptr, i64, ptr, i64, ptr)\n"
                  "declare i64 @sun_llvm_dict_get_bool_i64(ptr, i1, i64)\n"
                  "declare double @sun_llvm_dict_get_bool_double(ptr, i1, double)\n"
                  "declare i1 @sun_llvm_dict_get_bool_bool(ptr, i1, i1)\n"
                  "declare ptr @sun_llvm_dict_get_bool_string(ptr, i1, ptr, i64, ptr)\n"
                  "declare ptr @sun_llvm_dict_get_i64_ptr(ptr, i64, ptr)\n"
                  "declare ptr @sun_llvm_dict_get_string_ptr(ptr, ptr, i64, ptr)\n"
                  "declare ptr @sun_llvm_dict_get_bool_ptr(ptr, i1, ptr)\n"
                  "declare ptr @sun_llvm_dict_keys_i64(ptr)\n"
                  "declare ptr @sun_llvm_dict_keys_string(ptr)\n"
                  "declare ptr @sun_llvm_dict_keys_bool(ptr)\n"
                  "declare ptr @sun_llvm_dict_values_i64(ptr)\n"
                  "declare ptr @sun_llvm_dict_values_double(ptr)\n"
                  "declare ptr @sun_llvm_dict_values_bool(ptr)\n"
                  "declare ptr @sun_llvm_dict_values_string(ptr)\n"
                  "declare ptr @sun_llvm_dict_values_ptr(ptr)\n"
                  "declare void @sun_llvm_dict_set_i64_i64(ptr, i64, i64)\n"
                  "declare void @sun_llvm_dict_set_i64_double(ptr, i64, double)\n"
                  "declare void @sun_llvm_dict_set_i64_bool(ptr, i64, i1)\n"
                  "declare void @sun_llvm_dict_set_i64_string(ptr, i64, ptr, i64)\n"
                  "declare void @sun_llvm_dict_set_string_i64(ptr, ptr, i64, i64)\n"
                  "declare void @sun_llvm_dict_set_string_double(ptr, ptr, i64, double)\n"
                  "declare void @sun_llvm_dict_set_string_bool(ptr, ptr, i64, i1)\n"
                  "declare void @sun_llvm_dict_set_string_string(ptr, ptr, i64, ptr, i64)\n"
                  "declare void @sun_llvm_dict_set_bool_i64(ptr, i1, i64)\n"
                  "declare void @sun_llvm_dict_set_bool_double(ptr, i1, double)\n"
                  "declare void @sun_llvm_dict_set_bool_bool(ptr, i1, i1)\n"
                  "declare void @sun_llvm_dict_set_bool_string(ptr, i1, ptr, i64)\n"
                  "declare void @sun_llvm_dict_set_i64_ptr(ptr, i64, ptr)\n"
                  "declare void @sun_llvm_dict_set_string_ptr(ptr, ptr, i64, ptr)\n"
                  "declare void @sun_llvm_dict_set_bool_ptr(ptr, i1, ptr)\n"
                  "declare ptr @sun_llvm_list_create_i64()\n"
                  "declare ptr @sun_llvm_list_create_double()\n"
                  "declare ptr @sun_llvm_list_create_bool()\n"
                  "declare ptr @sun_llvm_list_create_string()\n"
                  "declare ptr @sun_llvm_list_create_ptr()\n"
                  "declare void @sun_llvm_list_append_i64(ptr, i64)\n"
                  "declare void @sun_llvm_list_append_double(ptr, double)\n"
                  "declare void @sun_llvm_list_append_bool(ptr, i1)\n"
                  "declare void @sun_llvm_list_append_string(ptr, ptr, i64)\n"
                  "declare void @sun_llvm_list_append_ptr(ptr, ptr)\n"
                  "declare i64 @sun_llvm_list_at_i64(ptr, double)\n"
                  "declare double @sun_llvm_list_at_double(ptr, double)\n"
                  "declare i1 @sun_llvm_list_at_bool(ptr, double)\n"
                  "declare ptr @sun_llvm_list_at_string(ptr, double, ptr)\n"
                  "declare ptr @sun_llvm_list_at_ptr(ptr, double)\n"
                  "declare i64 @sun_llvm_list_len_i64(ptr)\n"
                  "declare i64 @sun_llvm_list_len_double(ptr)\n"
                  "declare i64 @sun_llvm_list_len_bool(ptr)\n"
                  "declare i64 @sun_llvm_list_len_string(ptr)\n"
                  "declare i64 @sun_llvm_list_len_ptr(ptr)\n"
                  "declare void @sun_llvm_list_clear_i64(ptr)\n"
                  "declare void @sun_llvm_list_clear_double(ptr)\n"
                  "declare void @sun_llvm_list_clear_bool(ptr)\n"
                  "declare void @sun_llvm_list_clear_string(ptr)\n"
                  "declare void @sun_llvm_list_clear_ptr(ptr)\n"
                  "declare i1 @sun_llvm_list_contains_i64(ptr, i64)\n"
                  "declare i1 @sun_llvm_list_contains_double(ptr, double)\n"
                  "declare i1 @sun_llvm_list_contains_bool(ptr, i1)\n"
                  "declare i1 @sun_llvm_list_contains_string(ptr, ptr, i64)\n"
                  "declare i1 @sun_llvm_list_contains_ptr(ptr, ptr)\n"
                  "declare void @sun_llvm_list_insert_i64(ptr, double, i64)\n"
                  "declare void @sun_llvm_list_insert_double(ptr, double, double)\n"
                  "declare void @sun_llvm_list_insert_bool(ptr, double, i1)\n"
                  "declare void @sun_llvm_list_insert_string(ptr, double, ptr, i64)\n"
                  "declare void @sun_llvm_list_insert_ptr(ptr, double, ptr)\n"
                  "declare i1 @sun_llvm_list_remove_i64(ptr, i64)\n"
                  "declare i1 @sun_llvm_list_remove_double(ptr, double)\n"
                  "declare i1 @sun_llvm_list_remove_bool(ptr, i1)\n"
                  "declare i1 @sun_llvm_list_remove_string(ptr, ptr, i64)\n"
                  "declare i1 @sun_llvm_list_remove_ptr(ptr, ptr)\n"
                  "declare i64 @sun_llvm_list_pop_i64(ptr, double)\n"
                  "declare double @sun_llvm_list_pop_double(ptr, double)\n"
                  "declare i1 @sun_llvm_list_pop_bool(ptr, double)\n"
                  "declare ptr @sun_llvm_list_pop_string(ptr, double, ptr)\n"
                  "declare ptr @sun_llvm_list_pop_ptr(ptr, double)\n"
                  "declare void @sun_llvm_list_set_i64(ptr, double, i64)\n"
                  "declare void @sun_llvm_list_set_double(ptr, double, double)\n"
                  "declare void @sun_llvm_list_set_bool(ptr, double, i1)\n"
                  "declare void @sun_llvm_list_set_string(ptr, double, ptr, i64)\n"
                  "declare void @sun_llvm_list_set_ptr(ptr, double, ptr)\n"
                  "declare void @llvm.trap()\n"
                  "declare { i64, i1 } @llvm.sadd.with.overflow.i64(i64, i64)\n"
                  "declare { i64, i1 } @llvm.ssub.with.overflow.i64(i64, i64)\n"
                  "declare { i64, i1 } @llvm.smul.with.overflow.i64(i64, i64)\n\n"
               ;
        for (const auto& function : function_ir) module << '\n' << function;
        module << "\ndefine i32 @main() personality ptr @__gxx_personality_v0 {\nentry:\n"
               << main_ir;
        module << "}\n";
        return module.str();
    }

private:
    void reset_function_state() {
        out_.str(std::string());
        out_.clear();
        variables_.clear();
        loop_targets_.clear();
        finally_stack_.clear();
        exception_target_.clear();
        next_temp_ = 0;
        next_label_ = 0;
        terminated_ = false;
        current_owner_type_.clear();
        capture_slots_.clear();
        capture_lengths_.clear();
    }

    std::vector<Field> custom_fields(const std::string& name) const {
        const auto found = custom_types_.find(name);
        if (found == custom_types_.end()) {
            throw std::logic_error("unknown LLVM custom type '" + name + "'");
        }
        std::vector<Field> fields;
        if (!found->second->base_type.empty()) {
            fields = custom_fields(found->second->base_type);
        }
        fields.insert(fields.end(), found->second->fields.begin(),
                      found->second->fields.end());
        return fields;
    }

    std::size_t custom_field_index(const std::string& type_name,
                                   const std::string& field_name) const {
        const auto fields = custom_fields(type_name);
        for (std::size_t i = 0; i < fields.size(); ++i) {
            if (fields[i].name == field_name) return i;
        }
        throw std::logic_error("unknown LLVM field '" + field_name +
                               "' on '" + type_name + "'");
    }

    std::size_t enum_variant_index(const std::string& type_name,
                                   const std::string& variant_name) const {
        const auto& variants = custom_types_.at(type_name)->enum_variants;
        for (std::size_t i = 0; i < variants.size(); ++i) {
            if (variants[i].name == variant_name) return i;
        }
        throw std::logic_error("unknown LLVM enum variant '" + variant_name +
                               "' on '" + type_name + "'");
    }

    std::string enum_variant_name(const std::string& encoded_name) const {
        const auto separator = encoded_name.rfind('_');
        if (separator == std::string::npos) {
            throw std::logic_error("invalid LLVM enum variant name");
        }
        return encoded_name.substr(separator + 1);
    }

    Value emit_enum_constructor(const std::string& type_name,
                                const std::string& variant_name,
                                const std::vector<Expression>& arguments) {
        const auto& type = *custom_types_.at(type_name);
        const auto index = enum_variant_index(type_name, variant_name);
        const auto& variant = type.enum_variants[index];
        if (arguments.size() != variant.fields.size()) {
            throw std::logic_error("LLVM enum payload arity mismatch");
        }
        const auto size_pointer = temporary();
        const auto size = temporary();
        const auto value = temporary();
        out_ << "  " << size_pointer << " = getelementptr %sun.type."
             << type_name << ", ptr null, i32 1\n"
             << "  " << size << " = ptrtoint ptr " << size_pointer << " to i64\n"
             << "  " << value << " = call ptr @malloc(i64 " << size << ")\n";
        const auto tag_pointer = temporary();
        out_ << "  " << tag_pointer << " = getelementptr %sun.type."
             << type_name << ", ptr " << value << ", i32 0, i32 0\n"
             << "  store i32 " << index << ", ptr " << tag_pointer << '\n';
        for (std::size_t i = 0; i < variant.fields.size(); ++i) {
            const auto payload = emit_expression(arguments[i]);
            const auto first_slot = temporary();
            const auto first_pointer = temporary();
            out_ << "  " << first_slot << " = getelementptr %sun.type."
                 << type_name << ", ptr " << value << ", i32 0, i32 1, i64 "
                 << (i * 2) << '\n'
                 << "  " << first_pointer << " = getelementptr i64, ptr "
                 << first_slot << ", i64 0\n";
            if (is_string_like(variant.fields[i].type)) {
                const auto pointer_value = temporary();
                const auto second_slot = temporary();
                out_ << "  " << pointer_value << " = ptrtoint ptr "
                     << payload.operand << " to i64\n"
                     << "  store i64 " << pointer_value << ", ptr "
                     << first_pointer << '\n'
                     << "  " << second_slot << " = getelementptr %sun.type."
                     << type_name << ", ptr " << value << ", i32 0, i32 1, i64 "
                     << (i * 2 + 1) << '\n'
                     << "  store i64 " << payload.string_length << ", ptr "
                     << second_slot << '\n';
            } else if (variant.fields[i].type == Type::number) {
                const auto bits = temporary();
                out_ << "  " << bits << " = bitcast double " << payload.operand
                     << " to i64\n"
                     << "  store i64 " << bits << ", ptr " << first_pointer
                     << '\n';
            } else if (variant.fields[i].type == Type::boolean) {
                const auto bits = temporary();
                out_ << "  " << bits << " = zext i1 " << payload.operand
                     << " to i64\n"
                     << "  store i64 " << bits << ", ptr " << first_pointer
                     << '\n';
            } else {
                out_ << "  store i64 " << payload.operand << ", ptr "
                     << first_pointer << '\n';
            }
        }
        return {Type{Type::Kind::custom, type_name}, value};
    }

    std::string emit_enum_tag(const Value& value) {
        const auto pointer = temporary();
        const auto tag = temporary();
        out_ << "  " << pointer << " = getelementptr %sun.type."
             << value.type.name << ", ptr " << value.operand
             << ", i32 0, i32 0\n"
             << "  " << tag << " = load i32, ptr " << pointer << '\n';
        return tag;
    }

    std::string box_collection_item(const Value& value) {
        if (value.type.kind != Type::Kind::tuple) return value.operand;
        const auto llvm_aggregate_type = llvm_type(value.type);
        const auto size_pointer = temporary();
        const auto size = temporary();
        const auto result = temporary();
        out_ << "  " << size_pointer << " = getelementptr "
             << llvm_aggregate_type << ", ptr null, i32 1\n"
             << "  " << size << " = ptrtoint ptr " << size_pointer << " to i64\n"
             << "  " << result << " = call ptr @malloc(i64 " << size << ")\n"
             << "  store " << llvm_aggregate_type << ' ' << value.operand
             << ", ptr " << result << '\n';
        return result;
    }

    Value unbox_collection_item(Type type, const std::string& pointer) {
        if (type.kind != Type::Kind::tuple) return {type, pointer};
        const auto result = temporary();
        out_ << "  " << result << " = load " << llvm_type(type)
             << ", ptr " << pointer << '\n';
        return {type, result};
    }

    void collect_expression_names(const Expression& expression,
                                  std::set<std::string>& names) const {
        if (expression.kind == Expression::Kind::variable &&
            expression.value != "(*this)") {
            names.insert(expression.value);
        }
        for (const auto& child : expression.children) {
            collect_expression_names(child, names);
        }
    }

    void collect_local_function_statements(
        const Block& block,
        std::vector<std::pair<const Statement*, const LocalFunctionStatement*>>& found) const {
        const auto visit = [&](const Block& nested, const auto& self) -> void {
            for (const auto& statement : nested) {
                if (const auto* local =
                        std::get_if<LocalFunctionStatement>(&statement.node)) {
                    found.emplace_back(&statement, local);
                } else if (const auto* conditional =
                               std::get_if<IfStatement>(&statement.node)) {
                    for (const auto& branch : conditional->branches) self(branch.body, self);
                    if (conditional->else_body) self(*conditional->else_body, self);
                } else if (const auto* matched =
                               std::get_if<MatchStatement>(&statement.node)) {
                    for (const auto& match_case : matched->cases) self(match_case.body, self);
                    if (matched->else_body) self(*matched->else_body, self);
                } else if (const auto* loop =
                               std::get_if<WhileStatement>(&statement.node)) {
                    self(loop->body, self);
                } else if (const auto* loop =
                               std::get_if<RangeForStatement>(&statement.node)) {
                    self(loop->body, self);
                } else if (const auto* loop =
                               std::get_if<ForEachStatement>(&statement.node)) {
                    self(loop->body, self);
                } else if (const auto* attempt =
                               std::get_if<TryCatchStatement>(&statement.node)) {
                    self(attempt->try_body, self);
                    for (const auto& handler : attempt->handlers) self(handler.body, self);
                    if (attempt->finally_body) self(*attempt->finally_body, self);
                } else if (const auto* with_open =
                               std::get_if<WithOpenStatement>(&statement.node)) {
                    self(with_open->body, self);
                }
            }
        };
        visit(block, visit);
    }

    void collect_local_function_scope(
        const Block& block, std::unordered_map<std::string, Type>& locals,
        std::set<std::string>& references) const {
        const auto add_expression = [&](const Expression& expression) {
            collect_expression_names(expression, references);
        };
        const auto visit = [&](const Block& nested, const auto& self) -> void {
            for (const auto& statement : nested) {
                if (const auto* declaration =
                        std::get_if<VariableDeclarationStatement>(&statement.node)) {
                    locals.emplace(declaration->cpp_name, declaration->type);
                    add_expression(declaration->initializer);
                } else if (const auto* assignment =
                               std::get_if<AssignmentStatement>(&statement.node)) {
                    references.insert(assignment->cpp_name);
                    add_expression(assignment->value);
                } else if (const auto* member =
                               std::get_if<MemberAssignmentStatement>(&statement.node)) {
                    add_expression(member->target);
                    add_expression(member->value);
                } else if (const auto* returned =
                               std::get_if<ReturnStatement>(&statement.node)) {
                    if (returned->value) add_expression(*returned->value);
                } else if (const auto* call =
                               std::get_if<CallStatement>(&statement.node)) {
                    for (const auto& argument : call->arguments) add_expression(argument);
                } else if (const auto* call =
                               std::get_if<MemberCallStatement>(&statement.node)) {
                    add_expression(call->receiver);
                    for (const auto& argument : call->arguments) add_expression(argument);
                } else if (const auto* indexed =
                               std::get_if<IndexedAssignmentStatement>(&statement.node)) {
                    add_expression(indexed->object);
                    add_expression(indexed->key);
                    add_expression(indexed->value);
                } else if (const auto* dictionary =
                               std::get_if<DictionaryAssignmentStatement>(&statement.node)) {
                    add_expression(dictionary->dictionary);
                    add_expression(dictionary->key);
                    add_expression(dictionary->value);
                } else if (const auto* printed =
                               std::get_if<PrintStatement>(&statement.node)) {
                    add_expression(printed->value);
                } else if (const auto* printed =
                               std::get_if<PrintColorStatement>(&statement.node)) {
                    add_expression(printed->value);
                } else if (const auto* assertion =
                               std::get_if<AssertStatement>(&statement.node)) {
                    add_expression(assertion->condition);
                } else if (const auto* raised =
                               std::get_if<RaiseStatement>(&statement.node)) {
                    add_expression(raised->error);
                } else if (const auto* conditional =
                               std::get_if<IfStatement>(&statement.node)) {
                    for (const auto& branch : conditional->branches) {
                        add_expression(branch.condition);
                        self(branch.body, self);
                    }
                    if (conditional->else_body) self(*conditional->else_body, self);
                } else if (const auto* matched =
                               std::get_if<MatchStatement>(&statement.node)) {
                    add_expression(matched->value);
                    for (const auto& match_case : matched->cases) {
                        add_expression(match_case.pattern);
                        for (const auto& binding : match_case.bindings) {
                            const auto prefix = "SunType_" + matched->value.type.name + "_";
                            if (match_case.enum_variant_type.rfind(prefix, 0) == 0) {
                                const auto variant_name =
                                    match_case.enum_variant_type.substr(prefix.size());
                                const auto& variants =
                                    custom_types_.at(matched->value.type.name)->enum_variants;
                                const auto variant = std::find_if(
                                    variants.begin(), variants.end(),
                                    [&](const EnumVariant& candidate) {
                                        return candidate.name == variant_name;
                                    });
                                if (variant != variants.end()) {
                                    const auto field = std::find_if(
                                        variant->fields.begin(), variant->fields.end(),
                                        [&](const Field& candidate) {
                                            return candidate.name == binding.field_name;
                                        });
                                    if (field != variant->fields.end()) {
                                        locals.emplace(binding.cpp_name, field->type);
                                    }
                                }
                            }
                        }
                        self(match_case.body, self);
                    }
                    if (matched->else_body) self(*matched->else_body, self);
                } else if (const auto* loop =
                               std::get_if<WhileStatement>(&statement.node)) {
                    add_expression(loop->condition);
                    self(loop->body, self);
                } else if (const auto* loop =
                               std::get_if<RangeForStatement>(&statement.node)) {
                    locals.emplace(loop->variable_name, Type::number);
                    add_expression(loop->start_value);
                    add_expression(loop->stop_value);
                    add_expression(loop->step_value);
                    self(loop->body, self);
                } else if (const auto* loop =
                               std::get_if<ForEachStatement>(&statement.node)) {
                    if (loop->collection.type == Type::bytes) {
                        locals.emplace(loop->variable_name, Type::integer);
                    } else if (loop->dictionary &&
                               is_dictionary_type(loop->collection.type)) {
                        locals.emplace(loop->variable_name,
                                       dictionary_key_type(loop->collection.type));
                    } else if (is_list_type(loop->collection.type)) {
                        locals.emplace(loop->variable_name,
                                       list_item_type(loop->collection.type));
                    }
                    add_expression(loop->collection);
                    self(loop->body, self);
                } else if (const auto* attempt =
                               std::get_if<TryCatchStatement>(&statement.node)) {
                    self(attempt->try_body, self);
                    for (const auto& handler : attempt->handlers) {
                        if (handler.message_name) {
                            locals.emplace(*handler.message_name, Type::string);
                        }
                        self(handler.body, self);
                    }
                    if (attempt->finally_body) self(*attempt->finally_body, self);
                } else if (const auto* with_open =
                               std::get_if<WithOpenStatement>(&statement.node)) {
                    locals.emplace(with_open->handle_name, Type::integer);
                    add_expression(with_open->path);
                    add_expression(with_open->mode);
                    self(with_open->body, self);
                }
            }
        };
        visit(block, visit);
    }

    void prepare_local_functions(const Block& block,
                                const std::unordered_map<std::string, Type>& parent_scope) {
        std::vector<std::pair<const Statement*, const LocalFunctionStatement*>> found;
        collect_local_function_statements(block, found);
        for (const auto& [statement, local] : found) {
            if (local_functions_.count(local->cpp_name)) continue;
            std::unordered_map<std::string, Type> local_scope;
            std::set<std::string> references;
            collect_local_function_scope(local->body, local_scope, references);
            for (const auto& parameter : local->parameters) {
                local_scope.emplace("sun_arg_" + parameter.name, parameter.type);
            }
            LocalFunctionInfo info;
            info.function.name = local->cpp_name;
            info.function.parameters = local->parameters;
            info.function.return_type = local->return_type;
            info.function.body = local->body;
            info.function.line = statement->line;
            for (const auto& reference : references) {
                if (local_scope.count(reference)) continue;
                const auto type = parent_scope.find(reference);
                if (type != parent_scope.end()) {
                    info.captures.emplace_back(reference, type->second);
                }
            }
            std::sort(info.captures.begin(), info.captures.end(),
                      [](const auto& left, const auto& right) {
                          return left.first < right.first;
                      });
            auto [inserted, created] =
                local_functions_.emplace(local->cpp_name, std::move(info));
            if (!created) continue;
            functions_.emplace(local->cpp_name, &inserted->second.function);
            local_function_order_.push_back(local->cpp_name);

            auto nested_scope = parent_scope;
            for (const auto& capture : inserted->second.captures) {
                nested_scope[capture.first] = capture.second;
            }
            for (const auto& parameter : local->parameters) {
                nested_scope["sun_arg_" + parameter.name] = parameter.type;
            }
            for (const auto& [name, type] : local_scope) {
                nested_scope[name] = type;
            }
            prepare_local_functions(local->body, nested_scope);
        }
    }

    std::string slot_pointer(const std::string& name) const {
        const auto captured = capture_slots_.find(name);
        return captured == capture_slots_.end()
                   ? "ptr %sun.slot." + name
                   : "ptr " + captured->second;
    }

    std::string length_pointer(const std::string& name) const {
        const auto captured = capture_lengths_.find(name);
        return captured == capture_lengths_.end()
                   ? "ptr %sun.len." + name
                   : "ptr " + captured->second;
    }

    void emit_captured_arguments(std::ostringstream& call,
                                 const std::string& function_name) {
        const auto found = local_functions_.find(function_name);
        if (found == local_functions_.end()) return;
        for (const auto& [name, type] : found->second.captures) {
            if (!call.str().empty()) call << ", ";
            call << slot_pointer(name);
            if (is_string_like(type)) {
                call << ", " << length_pointer(name);
            }
        }
    }

    const Function* find_method(const std::string& type_name,
                                const std::string& method) const {
        auto current = type_name;
        while (!current.empty()) {
            const auto found = methods_.find(current + "." + method);
            if (found != methods_.end()) return found->second;
            const auto type = custom_types_.find(current);
            if (type == custom_types_.end()) break;
            current = type->second->base_type;
        }
        return nullptr;
    }

    std::string emit_field_pointer(const Value& object,
                                   const std::string& field_name) {
        if (object.type.kind != Type::Kind::custom ||
            !custom_types_.count(object.type.name) ||
            custom_types_.at(object.type.name)->is_enum) {
            unsupported("field access on a non-class value");
        }
        const auto index = custom_field_index(object.type.name, field_name);
        const auto pointer = temporary();
        out_ << "  " << pointer << " = getelementptr %sun.type."
             << object.type.name << ", ptr " << object.operand
             << ", i32 0, i32 " << index << '\n';
        return pointer;
    }

    std::string clone_custom_value(const Value& value) {
        if (value.type.kind != Type::Kind::custom ||
            !custom_types_.count(value.type.name) ||
            custom_types_.at(value.type.name)->is_enum ||
            custom_types_.at(value.type.name)->is_error) {
            return value.operand;
        }
        const auto size_pointer = temporary();
        const auto size = temporary();
        const auto result = temporary();
        out_ << "  " << size_pointer << " = getelementptr %sun.type."
             << value.type.name << ", ptr null, i32 1\n"
             << "  " << size << " = ptrtoint ptr " << size_pointer << " to i64\n"
             << "  " << result << " = call ptr @malloc(i64 " << size << ")\n"
             << "  call void @llvm.memcpy.p0.p0.i64(ptr " << result << ", ptr "
             << value.operand << ", i64 " << size << ", i1 false)\n";
        return result;
    }

    void emit_allocas() {
        for (const auto& [name, type] : variables_) {
            if (capture_slots_.count(name)) continue;
            out_ << "  %sun.slot." << name << " = alloca " << llvm_type(type) << '\n';
            if (is_string_like(type)) {
                out_ << "  %sun.len." << name << " = alloca i64\n";
            }
        }
        if (!variables_.empty()) out_ << '\n';
    }

    std::string emit_function(const Function& function) {
        reset_function_state();
        current_owner_type_ = function.owner_type;
        const auto local_info = local_functions_.find(function.name);
        if (local_info != local_functions_.end()) {
            for (const auto& [name, type] : local_info->second.captures) {
                variables_.emplace(name, type);
                capture_slots_[name] = "%sun.capture." + name;
                if (is_string_like(type)) {
                    capture_lengths_[name] = "%sun.capture.len." + name;
                }
            }
        }
        for (const auto& parameter : function.parameters) {
            variables_.emplace("sun_arg_" + parameter.name, parameter.type);
        }
        predeclare_block(function.body);
        prepare_local_functions(function.body, variables_);
        std::ostringstream signature;
        signature << "define "
                  << (function.return_type ? llvm_type(*function.return_type) : "void")
                  << " @" << (!function.owner_type.empty()
                                  ? "sun_method_" + function.owner_type + "_" +
                                        function.name
                              : local_info != local_functions_.end()
                                  ? function.name
                                  : "sun_fn_" + function.name)
                  << '(';
        bool first = true;
        if (!function.owner_type.empty()) {
            signature << "ptr %sun.this";
            first = false;
        }
        for (const auto& parameter : function.parameters) {
            if (!first) signature << ", ";
            first = false;
            const auto name = "%sun_arg_" + parameter.name;
            signature << llvm_type(parameter.type) << ' ' << name;
            if (is_string_like(parameter.type)) {
                signature << ", i64 " << name << ".len";
            }
        }
        if (local_info != local_functions_.end()) {
            for (const auto& [name, type] : local_info->second.captures) {
                if (!first) signature << ", ";
                first = false;
                signature << "ptr %sun.capture." << name;
                if (is_string_like(type)) {
                    signature << ", ptr %sun.capture.len." << name;
                }
            }
        }
        if (function.return_type && is_string_like(*function.return_type)) {
            if (!first) signature << ", ";
            signature << "ptr %sun.return.len";
        }
        signature << ") personality ptr @__gxx_personality_v0 {\nentry:\n";
        emit_allocas();
        if (!function.owner_type.empty()) {
            out_ << "  %sun.this.slot = alloca ptr\n"
                 << "  store ptr %sun.this, ptr %sun.this.slot\n";
        }
        for (const auto& parameter : function.parameters) {
            const auto name = "sun_arg_" + parameter.name;
            out_ << "  store " << llvm_type(parameter.type) << " %" << name
                 << ", ptr %sun.slot." << name << '\n';
            if (is_string_like(parameter.type)) {
                out_ << "  store i64 %" << name << ".len, ptr %sun.len."
                     << name << '\n';
            }
        }
        emit_block(function.body);
        if (!terminated_) out_ << "  ret void\n";
        return signature.str() + out_.str() + "}\n";
    }

    void emit_call_arguments(std::ostringstream& call,
                             const std::vector<Expression>& arguments,
                             const Function& function) {
        bool first = true;
        for (std::size_t i = 0; i < arguments.size(); ++i) {
            auto value = emit_expression(arguments[i]);
            if (value.type.kind == Type::Kind::custom) {
                value.operand = clone_custom_value(value);
            }
            if (!first) call << ", ";
            first = false;
            call << llvm_type(value.type) << ' ' << value.operand;
            if (is_string_like(function.parameters.at(i).type)) {
                call << ", i64 " << value.string_length;
            }
        }
    }

    Value emit_custom_constructor(const std::string& type_name,
                                  const std::vector<Expression>& arguments) {
            const auto definition = custom_types_.find(type_name);
            if (definition == custom_types_.end() || definition->second->is_enum ||
                definition->second->is_error) {
                unsupported("constructor for '" + type_name + "'");
            }
            const auto size_pointer = temporary();
            const auto size = temporary();
            const auto object = temporary();
            out_ << "  " << size_pointer << " = getelementptr %sun.type."
                 << type_name << ", ptr null, i32 1\n"
                 << "  " << size << " = ptrtoint ptr " << size_pointer << " to i64\n"
                 << "  " << object << " = call ptr @malloc(i64 " << size << ")\n";
            const auto fields = custom_fields(type_name);
            for (std::size_t i = 0; i < fields.size(); ++i) {
                const auto pointer = temporary();
                out_ << "  " << pointer << " = getelementptr %sun.type."
                     << type_name << ", ptr " << object << ", i32 0, i32 " << i
                     << '\n';
                if (is_string_like(fields[i].type)) {
                    const auto data_pointer = temporary();
                    const auto length_pointer = temporary();
                    out_ << "  " << data_pointer << " = getelementptr { ptr, i64 }, ptr "
                         << pointer << ", i32 0, i32 0\n"
                         << "  " << length_pointer
                         << " = getelementptr { ptr, i64 }, ptr " << pointer
                         << ", i32 0, i32 1\n"
                         << "  store ptr null, ptr " << data_pointer << '\n'
                         << "  store i64 0, ptr " << length_pointer << '\n';
                } else {
                    out_ << "  store " << llvm_type(fields[i].type) << ' '
                         << (fields[i].type == Type::boolean ? "false"
                             : fields[i].type == Type::number ? "0.0" : "0")
                         << ", ptr " << pointer << '\n';
                }
            }

            const auto* initializer = find_method(type_name, "init");
            if (initializer) {
                std::ostringstream call;
                call << "ptr " << object;
                if (!arguments.empty()) call << ", ";
                emit_call_arguments(call, arguments, *initializer);
                out_ << "  call void @sun_method_" << initializer->owner_type
                     << "_init(" << call.str() << ")\n";
            } else {
                if (arguments.size() != fields.size()) {
                    throw std::logic_error("LLVM custom constructor field count mismatch");
                }
                for (std::size_t i = 0; i < arguments.size(); ++i) {
                    const auto value = emit_expression(arguments[i]);
                    const auto pointer = temporary();
                    out_ << "  " << pointer << " = getelementptr %sun.type."
                         << type_name << ", ptr " << object
                         << ", i32 0, i32 " << i << '\n';
                    if (is_string_like(fields[i].type)) {
                        const auto data_pointer = temporary();
                        const auto length_pointer = temporary();
                        out_ << "  " << data_pointer
                             << " = getelementptr { ptr, i64 }, ptr " << pointer
                             << ", i32 0, i32 0\n"
                             << "  " << length_pointer
                             << " = getelementptr { ptr, i64 }, ptr " << pointer
                             << ", i32 0, i32 1\n"
                             << "  store ptr " << value.operand << ", ptr "
                             << data_pointer << '\n'
                             << "  store i64 " << value.string_length << ", ptr "
                             << length_pointer << '\n';
                    } else {
                        out_ << "  store " << llvm_type(fields[i].type) << ' '
                             << value.operand << ", ptr " << pointer << '\n';
                    }
                }
            }
            return {Type{Type::Kind::custom, type_name}, object};
    }

    std::string checked_integer_helpers() const {
        return R"ir(define internal i64 @sun_checked_add(i64 %a, i64 %b) {
entry:
  %pair = call { i64, i1 } @llvm.sadd.with.overflow.i64(i64 %a, i64 %b)
  %value = extractvalue { i64, i1 } %pair, 0
  %overflow = extractvalue { i64, i1 } %pair, 1
  br i1 %overflow, label %fail, label %ok
fail:
  call void @llvm.trap()
  unreachable
ok:
  ret i64 %value
}

define internal i64 @sun_checked_sub(i64 %a, i64 %b) {
entry:
  %pair = call { i64, i1 } @llvm.ssub.with.overflow.i64(i64 %a, i64 %b)
  %value = extractvalue { i64, i1 } %pair, 0
  %overflow = extractvalue { i64, i1 } %pair, 1
  br i1 %overflow, label %fail, label %ok
fail:
  call void @llvm.trap()
  unreachable
ok:
  ret i64 %value
}

define internal i64 @sun_checked_mul(i64 %a, i64 %b) {
entry:
  %pair = call { i64, i1 } @llvm.smul.with.overflow.i64(i64 %a, i64 %b)
  %value = extractvalue { i64, i1 } %pair, 0
  %overflow = extractvalue { i64, i1 } %pair, 1
  br i1 %overflow, label %fail, label %ok
fail:
  call void @llvm.trap()
  unreachable
ok:
  ret i64 %value
}

define internal i64 @sun_checked_div(i64 %a, i64 %b) {
entry:
  %zero = icmp eq i64 %b, 0
  %min = icmp eq i64 %a, -9223372036854775808
  %negone = icmp eq i64 %b, -1
  %overflow = and i1 %min, %negone
  %invalid = or i1 %zero, %overflow
  br i1 %invalid, label %fail, label %ok
fail:
  call void @llvm.trap()
  unreachable
ok:
  %value = sdiv i64 %a, %b
  ret i64 %value
}

define internal i64 @sun_checked_mod(i64 %a, i64 %b) {
entry:
  %zero = icmp eq i64 %b, 0
  %min = icmp eq i64 %a, -9223372036854775808
  %negone = icmp eq i64 %b, -1
  %overflow = and i1 %min, %negone
  %invalid = or i1 %zero, %overflow
  br i1 %invalid, label %fail, label %ok
fail:
  call void @llvm.trap()
  unreachable
ok:
  %value = srem i64 %a, %b
  ret i64 %value
}

define internal i64 @sun_checked_neg(i64 %a) {
entry:
  %pair = call { i64, i1 } @llvm.ssub.with.overflow.i64(i64 0, i64 %a)
  %value = extractvalue { i64, i1 } %pair, 0
  %overflow = extractvalue { i64, i1 } %pair, 1
  br i1 %overflow, label %fail, label %ok
fail:
  call void @llvm.trap()
  unreachable
ok:
  ret i64 %value
}
)ir";
    }

        std::string checked_number_helpers() const {
            return R"ir(define internal i64 @sun_checked_number_to_int(double %value) {
entry:
  %finite = fcmp ord double %value, %value
  br i1 %finite, label %whole.check, label %fail
whole.check:
  %floor = call double @llvm.floor.f64(double %value)
  %whole = fcmp oeq double %value, %floor
  br i1 %whole, label %range.check, label %fail
range.check:
  %limit = fcmp oge double %value, 0xC3E0000000000000
  %upper = fcmp olt double %value, 0x43E0000000000000
  %range.valid = and i1 %limit, %upper
  br i1 %range.valid, label %convert, label %fail
convert:
  %integer = fptosi double %value to i64
  ret i64 %integer
fail:
  call void @llvm.trap()
  unreachable
}
)ir";
        }

        std::string checked_character_helper() const {
                return R"ir(define internal ptr @sun_checked_char(ptr %value, i64 %length) {
entry:
    %is.one = icmp eq i64 %length, 1
    br i1 %is.one, label %one, label %check.two
check.two:
    %is.two = icmp eq i64 %length, 2
    br i1 %is.two, label %two, label %check.three
check.three:
    %is.three = icmp eq i64 %length, 3
    br i1 %is.three, label %three, label %check.four
check.four:
    %is.four = icmp eq i64 %length, 4
    br i1 %is.four, label %four, label %fail
one:
    %one.byte = load i8, ptr %value
    %one.code = zext i8 %one.byte to i32
    %one.valid = icmp ult i32 %one.code, 128
    br i1 %one.valid, label %valid, label %fail
two:
    %two.byte0 = load i8, ptr %value
    %two.code0 = zext i8 %two.byte0 to i32
    %two.byte1.ptr = getelementptr i8, ptr %value, i64 1
    %two.byte1 = load i8, ptr %two.byte1.ptr
    %two.code1 = zext i8 %two.byte1 to i32
    %two.low = icmp uge i32 %two.code0, 194
    %two.high = icmp ule i32 %two.code0, 223
    %two.lead.valid = and i1 %two.low, %two.high
    %two.cont.low = icmp uge i32 %two.code1, 128
    %two.cont.high = icmp ule i32 %two.code1, 191
    %two.cont.valid = and i1 %two.cont.low, %two.cont.high
    %two.valid = and i1 %two.lead.valid, %two.cont.valid
    br i1 %two.valid, label %valid, label %fail
three:
    %three.byte0 = load i8, ptr %value
    %three.code0 = zext i8 %three.byte0 to i32
    %three.byte1.ptr = getelementptr i8, ptr %value, i64 1
    %three.byte1 = load i8, ptr %three.byte1.ptr
    %three.code1 = zext i8 %three.byte1 to i32
    %three.byte2.ptr = getelementptr i8, ptr %value, i64 2
    %three.byte2 = load i8, ptr %three.byte2.ptr
    %three.code2 = zext i8 %three.byte2 to i32
    %three.low = icmp uge i32 %three.code0, 224
    %three.high = icmp ule i32 %three.code0, 239
    %three.lead.valid = and i1 %three.low, %three.high
    %three.cont1.low = icmp uge i32 %three.code1, 128
    %three.cont1.high = icmp ule i32 %three.code1, 191
    %three.cont1.valid = and i1 %three.cont1.low, %three.cont1.high
    %three.cont2.low = icmp uge i32 %three.code2, 128
    %three.cont2.high = icmp ule i32 %three.code2, 191
    %three.cont2.valid = and i1 %three.cont2.low, %three.cont2.high
    %three.is.e0 = icmp eq i32 %three.code0, 224
    %three.not.e0 = icmp ne i32 %three.code0, 224
    %three.e0.low = icmp uge i32 %three.code1, 160
    %three.e0.valid = or i1 %three.not.e0, %three.e0.low
    %three.is.ed = icmp eq i32 %three.code0, 237
    %three.not.ed = icmp ne i32 %three.code0, 237
    %three.ed.high = icmp ule i32 %three.code1, 159
    %three.ed.valid = or i1 %three.not.ed, %three.ed.high
    %three.valid0 = and i1 %three.lead.valid, %three.cont1.valid
    %three.valid1 = and i1 %three.cont2.valid, %three.e0.valid
    %three.valid2 = and i1 %three.ed.valid, %three.valid0
    %three.valid = and i1 %three.valid1, %three.valid2
    br i1 %three.valid, label %valid, label %fail
four:
    %four.byte0 = load i8, ptr %value
    %four.code0 = zext i8 %four.byte0 to i32
    %four.byte1.ptr = getelementptr i8, ptr %value, i64 1
    %four.byte1 = load i8, ptr %four.byte1.ptr
    %four.code1 = zext i8 %four.byte1 to i32
    %four.byte2.ptr = getelementptr i8, ptr %value, i64 2
    %four.byte2 = load i8, ptr %four.byte2.ptr
    %four.code2 = zext i8 %four.byte2 to i32
    %four.byte3.ptr = getelementptr i8, ptr %value, i64 3
    %four.byte3 = load i8, ptr %four.byte3.ptr
    %four.code3 = zext i8 %four.byte3 to i32
    %four.low = icmp uge i32 %four.code0, 240
    %four.high = icmp ule i32 %four.code0, 244
    %four.lead.valid = and i1 %four.low, %four.high
    %four.cont1.low = icmp uge i32 %four.code1, 128
    %four.cont1.high = icmp ule i32 %four.code1, 191
    %four.cont1.valid = and i1 %four.cont1.low, %four.cont1.high
    %four.cont2.low = icmp uge i32 %four.code2, 128
    %four.cont2.high = icmp ule i32 %four.code2, 191
    %four.cont2.valid = and i1 %four.cont2.low, %four.cont2.high
    %four.cont3.low = icmp uge i32 %four.code3, 128
    %four.cont3.high = icmp ule i32 %four.code3, 191
    %four.cont3.valid = and i1 %four.cont3.low, %four.cont3.high
    %four.is.f0 = icmp eq i32 %four.code0, 240
    %four.not.f0 = icmp ne i32 %four.code0, 240
    %four.f0.low = icmp uge i32 %four.code1, 144
    %four.f0.valid = or i1 %four.not.f0, %four.f0.low
    %four.is.f4 = icmp eq i32 %four.code0, 244
    %four.not.f4 = icmp ne i32 %four.code0, 244
    %four.f4.high = icmp ule i32 %four.code1, 143
    %four.f4.valid = or i1 %four.not.f4, %four.f4.high
    %four.valid0 = and i1 %four.lead.valid, %four.cont1.valid
    %four.valid1 = and i1 %four.cont2.valid, %four.cont3.valid
    %four.valid2 = and i1 %four.f0.valid, %four.f4.valid
    %four.valid3 = and i1 %four.valid0, %four.valid1
    %four.valid = and i1 %four.valid2, %four.valid3
    br i1 %four.valid, label %valid, label %fail
fail:
    call void @llvm.trap()
    unreachable
valid:
    ret ptr %value
}
)ir";
        }

    [[noreturn]] void unsupported(const std::string& feature) const {
        throw std::logic_error("LLVM backend does not yet support " + feature);
    }

    std::string temporary() { return "%sun.tmp." + std::to_string(next_temp_++); }
    std::string label(const std::string& prefix) {
        return "sun." + prefix + "." + std::to_string(next_label_++);
    }

    std::string make_invokes(const std::string& body,
                             const std::string& unwind_label) {
        std::istringstream input(body);
        std::ostringstream result;
        std::string line;
        static const std::regex throwing_call(
            R"(@(sun_llvm_[A-Za-z0-9_]+|sun_fn_[A-Za-z0-9_]+|sun_method_[A-Za-z0-9_]+|sun_local_function_[A-Za-z0-9_]+)\()");
        while (std::getline(input, line)) {
            const auto call = line.find("call ");
            if (call != std::string::npos &&
                std::regex_search(line, throwing_call)) {
                const auto normal_label = label("invoke.cont");
                line.replace(call, 5, "invoke ");
                line += " to label %" + normal_label + " unwind label %" +
                        unwind_label;
                result << line << '\n' << normal_label << ":\n";
            } else {
                result << line << '\n';
            }
        }
        return result.str();
    }

    void emit_transformed_block(const Block& block,
                                const std::string& unwind_label) {
        const auto prefix = out_.str();
        emit_block(block);
        const auto generated = out_.str();
        out_.str(prefix + make_invokes(generated.substr(prefix.size()), unwind_label));
        out_.clear();
        out_.seekp(0, std::ios::end);
    }

    void emit_finally_stack() {
        const auto current_target = exception_target_;
        for (auto it = finally_stack_.rbegin(); it != finally_stack_.rend(); ++it) {
            exception_target_ = it->second;
            emit_block(*it->first);
        }
        exception_target_ = current_target;
    }

    void emit_try(const TryCatchStatement& statement) {
        for (const auto& handler : statement.handlers) {
            if (handler.cpp_error_type == "SunCppException") {
                unsupported("cppCatcher without inline C++ in LLVM");
            }
            if (handler.cpp_error_type != "SunType_AnError" &&
                handler.cpp_error_type.rfind("SunType_", 0) == 0 &&
                !custom_error_types_.count(
                    handler.cpp_error_type.substr(std::string("SunType_").size()))) {
                unsupported("unknown custom exception handler");
            }
        }
        const auto outer_target = exception_target_;
        const auto exception_label = label("try.exception");
        const auto unhandled_label = label("try.unhandled");
        const auto normal_label = label("try.normal");
        const auto done_label = label("try.end");

        if (statement.finally_body) {
            finally_stack_.emplace_back(&*statement.finally_body, outer_target);
        }
        exception_target_ = exception_label;
        const auto saved_body = out_.str();
        emit_block(statement.try_body);
        const auto generated_body = out_.str();
        out_.str(saved_body +
                 make_invokes(generated_body.substr(saved_body.size()), exception_label));
        out_.clear();
        out_.seekp(0, std::ios::end);
        const bool body_terminated = terminated_;
        if (!body_terminated) out_ << "  br label %" << normal_label << '\n';

        exception_target_ = outer_target;
        out_ << exception_label << ":\n"
             << "  %sun.landingpad." << next_label_ << " = landingpad { ptr, i32 }\n";
        const auto landingpad = "%sun.landingpad." + std::to_string(next_label_++);
        if (statement.finally_body) out_ << "    cleanup\n";
        if (!statement.handlers.empty()) {
            out_ << "    catch ptr @_ZTISt9exception\n";
        }
        if (statement.handlers.empty()) {
            if (statement.finally_body) {
                terminated_ = false;
                emit_block(*statement.finally_body);
            }
            out_ << "  resume { ptr, i32 } " << landingpad << '\n';
        } else {
            const auto selector = temporary();
            const auto expected_selector = temporary();
            const auto is_matched = temporary();
            const auto exception_object = temporary();
            const auto dispatch_label = label("try.dispatch");
            out_ << "  " << selector << " = extractvalue { ptr, i32 } "
                 << landingpad << ", 1\n"
                 << "  " << expected_selector
                 << " = call i32 @llvm.eh.typeid.for(ptr @_ZTISt9exception)\n"
                 << "  " << is_matched << " = icmp eq i32 " << selector << ", "
                 << expected_selector << '\n'
                 << "  " << exception_object << " = extractvalue { ptr, i32 } "
                 << landingpad << ", 0\n"
                 << "  br i1 " << is_matched << ", label %" << dispatch_label
                 << ", label %" << unhandled_label << '\n'
                 << dispatch_label << ":\n";

            std::vector<std::string> handler_labels;
            handler_labels.reserve(statement.handlers.size());
            for (std::size_t i = 0; i < statement.handlers.size(); ++i) {
                handler_labels.push_back(label("try.handler"));
            }
            for (std::size_t i = 0; i < statement.handlers.size(); ++i) {
                const auto& handler = statement.handlers[i];
                std::string matches = "true";
                if (handler.cpp_error_type != "SunType_AnError") {
                    const auto custom_name = handler.cpp_error_type.substr(
                        std::string("SunType_").size());
                    const auto name_pointer = string_pointer(custom_name);
                    matches = temporary();
                    out_ << "  " << matches
                         << " = call i1 @sun_llvm_exception_matches(ptr "
                         << exception_object << ", ptr " << name_pointer
                         << ", i64 " << custom_name.size() << ")\n";
                }
                const auto next_label = i + 1 == statement.handlers.size()
                                            ? unhandled_label
                                            : label("try.next_handler");
                out_ << "  br i1 " << matches << ", label %"
                     << handler_labels[i] << ", label %" << next_label << '\n';
                if (next_label != unhandled_label) {
                    out_ << next_label << ":\n";
                }
            }

            bool has_normal_exit = !body_terminated;
            for (std::size_t i = 0; i < statement.handlers.size(); ++i) {
                const auto& handler = statement.handlers[i];
                out_ << handler_labels[i] << ":\n";
                const auto message_length_slot = temporary();
                const auto message = temporary();
                out_ << "  " << message_length_slot << " = alloca i64\n"
                     << "  " << message << " = call ptr @sun_llvm_catch_exception(ptr "
                     << exception_object << ", ptr " << message_length_slot << ")\n";
                if (handler.message_name) {
                    const auto variable = variables_.find(*handler.message_name);
                    if (variable == variables_.end()) {
                        unsupported("exception message variable");
                    }
                    const auto message_length = temporary();
                    out_ << "  " << message_length << " = load i64, ptr "
                         << message_length_slot << '\n'
                         << "  store ptr " << message << ", ptr %sun.slot."
                         << *handler.message_name << '\n'
                         << "  store i64 " << message_length << ", ptr %sun.len."
                         << *handler.message_name << '\n';
                }
                terminated_ = false;
                exception_target_ = outer_target;
                emit_block(handler.body);
                if (!terminated_) {
                    has_normal_exit = true;
                    out_ << "  br label %" << normal_label << '\n';
                }
            }

            out_ << unhandled_label << ":\n";
            terminated_ = false;
            if (statement.finally_body) {
                exception_target_ = outer_target;
                emit_block(*statement.finally_body);
            }
            out_ << "  call void @sun_llvm_rethrow_exception(ptr "
                 << exception_object << ")\n"
                 << "  unreachable\n";
            if (has_normal_exit) {
                out_ << normal_label << ":\n";
                terminated_ = false;
                exception_target_ = outer_target;
                if (statement.finally_body) emit_block(*statement.finally_body);
                if (!terminated_) out_ << "  br label %" << done_label << '\n';
                out_ << done_label << ":\n";
                terminated_ = false;
            } else {
                terminated_ = true;
            }
        }
        if (statement.handlers.empty()) {
            if (body_terminated) {
                terminated_ = true;
            } else {
                out_ << normal_label << ":\n";
                terminated_ = false;
                exception_target_ = outer_target;
                if (statement.finally_body) emit_block(*statement.finally_body);
                if (!terminated_) out_ << "  br label %" << done_label << '\n';
                out_ << done_label << ":\n";
                terminated_ = false;
            }
        }
        if (statement.finally_body) finally_stack_.pop_back();
    }

    std::string string_pointer(const std::string& value) {
        for (const auto& entry : strings_) {
            if (entry.second == value) return entry.first;
        }
        const auto name = "@.sun.str." + std::to_string(strings_.size());
        strings_.emplace_back(name, value);
        return name;
    }

    Value emit_tuple_item(const Value& tuple, std::size_t index) {
        const auto items = llvm_tuple_types(tuple.type);
        const auto field = temporary();
        out_ << "  " << field << " = extractvalue " << llvm_type(tuple.type)
             << ' ' << tuple.operand << ", " << index << '\n';
        if (!is_string_like(items.at(index))) {
            return {items.at(index), field};
        }
        const auto pointer = temporary();
        const auto length = temporary();
        out_ << "  " << pointer << " = extractvalue { ptr, i64 } "
             << field << ", 0\n"
             << "  " << length << " = extractvalue { ptr, i64 } "
             << field << ", 1\n";
        return {items.at(index), pointer, length};
    }

    std::string emit_equal_values(Type type, const Value& left, const Value& right) {
        if (is_string_like(type)) {
            const auto same_length = temporary();
            const auto compare_length = temporary();
            const auto compared = temporary();
            const auto same_bytes = temporary();
            const auto equal = temporary();
            out_ << "  " << same_length << " = icmp eq i64 "
                 << left.string_length << ", " << right.string_length << '\n'
                 << "  " << compare_length << " = select i1 " << same_length
                 << ", i64 " << left.string_length << ", i64 0\n"
                 << "  " << compared << " = call i32 @memcmp(ptr "
                 << left.operand << ", ptr " << right.operand << ", i64 "
                 << compare_length << ")\n"
                 << "  " << same_bytes << " = icmp eq i32 " << compared << ", 0\n"
                 << "  " << equal << " = and i1 " << same_length << ", "
                 << same_bytes << '\n';
            return equal;
        }
        const auto equal = temporary();
        if (type == Type::number) {
            out_ << "  " << equal << " = fcmp oeq double "
                 << left.operand << ", " << right.operand << '\n';
        } else {
            out_ << "  " << equal << " = icmp eq " << llvm_type(type) << ' '
                 << left.operand << ", " << right.operand << '\n';
        }
        return equal;
    }

    std::string emit_tuple_equal(const Value& left, const Value& right) {
        const auto items = llvm_tuple_types(left.type);
        std::string equal = "true";
        for (std::size_t i = 0; i < items.size(); ++i) {
            const auto item_equal = emit_equal_values(
                items[i], emit_tuple_item(left, i), emit_tuple_item(right, i));
            const auto combined = temporary();
            out_ << "  " << combined << " = and i1 " << equal
                 << ", " << item_equal << '\n';
            equal = combined;
        }
        return equal;
    }

    void emit_print_tuple(const Value& tuple, bool newline = true) {
        const auto items = llvm_tuple_types(tuple.type);
        out_ << "  call i32 @putchar(i32 40)\n";
        for (std::size_t i = 0; i < items.size(); ++i) {
            if (i) out_ << "  call i32 (ptr, ...) @printf(ptr @.sun.fmt.comma)\n";
            emit_print_inline(emit_tuple_item(tuple, i));
        }
        if (items.size() == 1) out_ << "  call i32 @putchar(i32 44)\n";
        out_ << "  call i32 @putchar(i32 41)\n";
        if (newline) out_ << "  call i32 @putchar(i32 10)\n";
    }

    void emit_print_custom(const Value& value, bool newline = true) {
        const auto fields = custom_fields(value.type.name);
        const auto type_name = string_pointer(value.type.name + "{");
        out_ << "  call i32 (ptr, ...) @printf(ptr @.sun.fmt.string, ptr "
             << type_name << ")\n";
        for (std::size_t i = 0; i < fields.size(); ++i) {
            const auto prefix = string_pointer(
                std::string(i ? ", " : "") + fields[i].name + "=");
            out_ << "  call i32 (ptr, ...) @printf(ptr @.sun.fmt.string, ptr "
                 << prefix << ")\n";
            const auto pointer = emit_field_pointer(value, fields[i].name);
            if (is_string_like(fields[i].type)) {
                const auto data_pointer = temporary();
                const auto length_pointer = temporary();
                const auto data = temporary();
                const auto length = temporary();
                const auto stream = temporary();
                const auto ignored = temporary();
                out_ << "  " << data_pointer
                     << " = getelementptr { ptr, i64 }, ptr " << pointer
                     << ", i32 0, i32 0\n"
                     << "  " << length_pointer
                     << " = getelementptr { ptr, i64 }, ptr " << pointer
                     << ", i32 0, i32 1\n"
                     << "  " << data << " = load ptr, ptr " << data_pointer << '\n'
                     << "  " << length << " = load i64, ptr " << length_pointer
                     << '\n'
                     << "  " << stream << " = load ptr, ptr @stdout\n"
                     << "  " << ignored << " = call i64 @fwrite(ptr " << data
                     << ", i64 " << length << ", i64 1, ptr " << stream << ")\n";
            } else {
                const auto item = temporary();
                out_ << "  " << item << " = load " << llvm_type(fields[i].type)
                     << ", ptr " << pointer << '\n';
                if (fields[i].type == Type::boolean) {
                    const auto text = temporary();
                    out_ << "  " << text << " = select i1 " << item
                         << ", ptr @.sun.bool.true, ptr @.sun.bool.false\n"
                         << "  call i32 (ptr, ...) @printf(ptr @.sun.fmt.string, ptr "
                         << text << ")\n";
                } else if (fields[i].type == Type::integer) {
                    out_ << "  call i32 (ptr, ...) @printf(ptr @.sun.fmt.int.no_newline, i64 "
                         << item << ")\n";
                } else if (fields[i].type == Type::number) {
                    out_ << "  call i32 (ptr, ...) @printf(ptr @.sun.fmt.number.no_newline, double "
                         << item << ")\n";
                } else {
                    unsupported("printing class field '" + fields[i].name + "'");
                }
            }
        }
        out_ << "  call i32 (ptr, ...) @printf(ptr @.sun.fmt.string, ptr "
             << string_pointer("}") << ")\n";
        if (newline) out_ << "  call i32 @putchar(i32 10)\n";
    }

    void emit_print_enum(const Value& value, bool newline = true) {
        const auto tag = emit_enum_tag(value);
        std::string text = string_pointer("<invalid " + value.type.name + ">");
        const auto& variants = custom_types_.at(value.type.name)->enum_variants;
        for (std::size_t i = variants.size(); i > 0; --i) {
            const auto matched = temporary();
            const auto selected = temporary();
            const auto variant_text = string_pointer(variants[i - 1].name);
            out_ << "  " << matched << " = icmp eq i32 " << tag << ", "
                 << (i - 1) << '\n'
                 << "  " << selected << " = select i1 " << matched
                 << ", ptr " << variant_text << ", ptr " << text << '\n';
            text = selected;
        }
        if (newline) out_ << "  call i32 @puts(ptr " << text << ")\n";
        else out_ << "  call i32 (ptr, ...) @printf(ptr @.sun.fmt.string, ptr "
                  << text << ")\n";
    }

    Value emit_expression(const Expression& expression) {
        if (expression.kind == Expression::Kind::literal) {
            if (expression.type == Type::boolean) {
                return {Type::boolean, expression.value == "true" ? "true" : "false"};
            }
            if (expression.type == Type::integer) {
                return {Type::integer, expression.value};
            }
            if (expression.type == Type::number) {
                std::size_t parsed = 0;
                const auto number = std::stod(expression.value, &parsed);
                if (parsed != expression.value.size() || !std::isfinite(number)) {
                    unsupported("non-finite numeric literals");
                }
                return {Type::number, llvm_double(number)};
            }
            if (expression.type == Type::character) {
                return {Type::character, string_pointer(expression.value),
                        std::to_string(expression.value.size())};
            }
            unsupported("literal type '" + cpp_type(expression.type) + "'");
        }
        if (expression.kind == Expression::Kind::string_literal) {
            return {Type::string, string_pointer(expression.value),
                    std::to_string(expression.value.size())};
        }
        if (expression.kind == Expression::Kind::string_interpolation) {
            const auto value = emit_expression(expression.children.at(0));
            if (is_string_like(value.type)) return {Type::string, value.operand,
                                                   value.string_length};
            if (value.type == Type::integer) {
                const auto buffer = temporary();
                out_ << "  " << buffer << " = call ptr @malloc(i64 32)\n";
                const auto length = temporary();
                out_ << "  " << length << " = call i32 (ptr, i64, ptr, ...) "
                     << "@snprintf(ptr " << buffer << ", i64 32, ptr @.sun.fmt.int.raw, i64 "
                     << value.operand << ")\n";
                const auto length64 = temporary();
                out_ << "  " << length64 << " = sext i32 " << length << " to i64\n";
                return {Type::string, buffer, length64};
            }
            if (value.type == Type::number) {
                const auto buffer = temporary();
                out_ << "  " << buffer << " = call ptr @malloc(i64 64)\n";
                const auto length = temporary();
                out_ << "  " << length << " = call i32 (ptr, i64, ptr, ...) "
                     << "@snprintf(ptr " << buffer << ", i64 64, ptr @.sun.fmt.number.raw, double "
                     << value.operand << ")\n";
                const auto length64 = temporary();
                out_ << "  " << length64 << " = sext i32 " << length << " to i64\n";
                return {Type::string, buffer, length64};
            }
            if (value.type == Type::boolean) {
                const auto text = temporary();
                const auto length = temporary();
                out_ << "  " << text << " = select i1 " << value.operand
                     << ", ptr @.sun.bool.true, ptr @.sun.bool.false\n"
                     << "  " << length << " = select i1 " << value.operand
                     << ", i64 4, i64 5\n";
                return {Type::string, text, length};
            }
            unsupported("string interpolation type '" + cpp_type(value.type) + "'");
        }
        if (expression.kind == Expression::Kind::formatted_string) {
            std::vector<Value> parts;
            std::string total_length = "0";
            for (const auto& child : expression.children) {
                const auto part = emit_expression(child);
                const auto next_length = temporary();
                out_ << "  " << next_length << " = add i64 " << total_length
                     << ", " << part.string_length << '\n';
                total_length = next_length;
                parts.push_back(part);
            }
            const auto buffer = temporary();
            out_ << "  " << buffer << " = call ptr @malloc(i64 " << total_length << ")\n";
            std::string offset = "0";
            for (const auto& part : parts) {
                const auto destination = temporary();
                out_ << "  " << destination << " = getelementptr i8, ptr "
                     << buffer << ", i64 " << offset << '\n'
                     << "  call void @llvm.memcpy.p0.p0.i64(ptr " << destination
                     << ", ptr " << part.operand << ", i64 " << part.string_length
                     << ", i1 false)\n";
                const auto next_offset = temporary();
                out_ << "  " << next_offset << " = add i64 " << offset
                     << ", " << part.string_length << '\n';
                offset = next_offset;
            }
            return {Type::string, buffer, total_length};
        }
        if (expression.kind == Expression::Kind::empty_dictionary) {
            const auto result = temporary();
            out_ << "  " << result << " = call ptr @sun_llvm_dict_create()\n";
            return {Type::empty_dictionary, result};
        }
        if (expression.kind == Expression::Kind::aggregate &&
            expression.type.kind == Type::Kind::tuple) {
            const auto items = llvm_tuple_types(expression.type);
            if (items.size() != expression.children.size()) {
                throw std::logic_error("LLVM tuple aggregate has inconsistent arity");
            }
            std::string aggregate = "undef";
            for (std::size_t i = 0; i < items.size(); ++i) {
                const auto item = emit_expression(expression.children[i]);
                std::string field_value = item.operand;
                std::string field_type = llvm_type(items[i]);
                if (is_string_like(items[i])) {
                    const auto with_pointer = temporary();
                    const auto with_length = temporary();
                    out_ << "  " << with_pointer
                         << " = insertvalue { ptr, i64 } undef, ptr "
                         << item.operand << ", 0\n"
                         << "  " << with_length
                         << " = insertvalue { ptr, i64 } " << with_pointer
                         << ", i64 " << item.string_length << ", 1\n";
                    field_value = with_length;
                    field_type = "{ ptr, i64 }";
                }
                const auto inserted = temporary();
                out_ << "  " << inserted << " = insertvalue "
                     << llvm_type(expression.type) << ' ' << aggregate << ", "
                     << field_type << ' ' << field_value << ", " << i << '\n';
                aggregate = inserted;
            }
            return {expression.type, aggregate};
        }
        if (expression.kind == Expression::Kind::aggregate &&
            expression.type.kind == Type::Kind::custom &&
            custom_types_.count(expression.type.name) &&
            custom_types_.at(expression.type.name)->is_enum) {
            if (expression.children.size() != 1) {
                unsupported("enum constructor aggregate");
            }
            const auto& variant_expression = expression.children.front();
            const auto prefix = "SunType_" + expression.type.name + "_";
            if (variant_expression.value.rfind(prefix, 0) != 0) {
                unsupported("enum variant constructor");
            }
            return emit_enum_constructor(
                expression.type.name,
                variant_expression.value.substr(prefix.size()),
                variant_expression.children);
        }
        if (expression.kind == Expression::Kind::aggregate &&
            expression.type.kind == Type::Kind::custom &&
            custom_types_.count(expression.type.name) &&
            !custom_types_.at(expression.type.name)->is_enum) {
            return emit_custom_constructor(expression.type.name, expression.children);
        }
        if (expression.kind == Expression::Kind::dictionary) {
            const auto result = temporary();
            out_ << "  " << result << " = call ptr @sun_llvm_dict_create()\n";
            for (std::size_t i = 0; i + 1 < expression.children.size(); i += 2) {
                const auto key = emit_expression(expression.children[i]);
                const auto value = emit_expression(expression.children[i + 1]);
                const auto pointer_value = box_collection_item(value);
                const auto key_string = key.type == Type::string || key.type == Type::character;
                const auto value_string = is_string_like(value.type);
                if (!key_string && key.type != Type::integer && key.type != Type::boolean) unsupported("dictionary key type");
                if (key_string) {
                    if (value_string) out_ << "  call void @sun_llvm_dict_set_string_string(ptr " << result << ", ptr " << key.operand << ", i64 " << key.string_length << ", ptr " << value.operand << ", i64 " << value.string_length << ")\n";
                    else if (value.type == Type::integer) out_ << "  call void @sun_llvm_dict_set_string_i64(ptr " << result << ", ptr " << key.operand << ", i64 " << key.string_length << ", i64 " << value.operand << ")\n";
                    else if (value.type == Type::number) out_ << "  call void @sun_llvm_dict_set_string_double(ptr " << result << ", ptr " << key.operand << ", i64 " << key.string_length << ", double " << value.operand << ")\n";
                    else if (value.type == Type::boolean) out_ << "  call void @sun_llvm_dict_set_string_bool(ptr " << result << ", ptr " << key.operand << ", i64 " << key.string_length << ", i1 " << value.operand << ")\n";
                    else if (llvm_type(value.type) == "ptr" ||
                             value.type.kind == Type::Kind::tuple) out_ << "  call void @sun_llvm_dict_set_string_ptr(ptr " << result << ", ptr " << key.operand << ", i64 " << key.string_length << ", ptr " << pointer_value << ")\n";
                } else if (key.type == Type::boolean) {
                    if (value_string) out_ << "  call void @sun_llvm_dict_set_bool_string(ptr " << result << ", i1 " << key.operand << ", ptr " << value.operand << ", i64 " << value.string_length << ")\n";
                    else if (value.type == Type::integer) out_ << "  call void @sun_llvm_dict_set_bool_i64(ptr " << result << ", i1 " << key.operand << ", i64 " << value.operand << ")\n";
                    else if (value.type == Type::number) out_ << "  call void @sun_llvm_dict_set_bool_double(ptr " << result << ", i1 " << key.operand << ", double " << value.operand << ")\n";
                    else if (value.type == Type::boolean) out_ << "  call void @sun_llvm_dict_set_bool_bool(ptr " << result << ", i1 " << key.operand << ", i1 " << value.operand << ")\n";
                    else if (llvm_type(value.type) == "ptr" ||
                             value.type.kind == Type::Kind::tuple) out_ << "  call void @sun_llvm_dict_set_bool_ptr(ptr " << result << ", i1 " << key.operand << ", ptr " << pointer_value << ")\n";
                } else {
                    if (value_string) out_ << "  call void @sun_llvm_dict_set_i64_string(ptr " << result << ", i64 " << key.operand << ", ptr " << value.operand << ", i64 " << value.string_length << ")\n";
                    else if (value.type == Type::integer) out_ << "  call void @sun_llvm_dict_set_i64_i64(ptr " << result << ", i64 " << key.operand << ", i64 " << value.operand << ")\n";
                    else if (value.type == Type::number) out_ << "  call void @sun_llvm_dict_set_i64_double(ptr " << result << ", i64 " << key.operand << ", double " << value.operand << ")\n";
                    else if (value.type == Type::boolean) out_ << "  call void @sun_llvm_dict_set_i64_bool(ptr " << result << ", i64 " << key.operand << ", i1 " << value.operand << ")\n";
                    else if (llvm_type(value.type) == "ptr" ||
                             value.type.kind == Type::Kind::tuple) out_ << "  call void @sun_llvm_dict_set_i64_ptr(ptr " << result << ", i64 " << key.operand << ", ptr " << pointer_value << ")\n";
                }
            }
            return {expression.type, result};
        }
        if (expression.kind == Expression::Kind::list) {
            const auto item_type = list_item_type(expression.type);
            const auto suffix = llvm_list_suffix(item_type);
            const auto result = temporary();
            out_ << "  " << result << " = call ptr @sun_llvm_list_create_" << suffix << "()\n";
            for (const auto& child : expression.children) {
                const auto value = emit_expression(child);
                if (is_string_like(item_type)) {
                    out_ << "  call void @sun_llvm_list_append_string(ptr " << result
                         << ", ptr " << value.operand << ", i64 " << value.string_length << ")\n";
                } else if (item_type.kind == Type::Kind::tuple) {
                    const auto boxed = box_collection_item(value);
                    out_ << "  call void @sun_llvm_list_append_ptr(ptr " << result
                         << ", ptr " << boxed << ")\n";
                } else {
                    out_ << "  call void @sun_llvm_list_append_" << suffix << "(ptr " << result
                         << ", " << llvm_type(item_type) << " " << value.operand << ")\n";
                }
            }
            return {expression.type, result};
        }
        if (expression.kind == Expression::Kind::variable) {
            if (expression.type.kind == Type::Kind::custom &&
                custom_types_.count(expression.type.name) &&
                custom_types_.at(expression.type.name)->is_enum &&
                expression.value.find("::") != std::string::npos) {
                const auto separator = expression.value.rfind("::");
                if (separator == std::string::npos) {
                    unsupported("enum value '" + expression.value + "'");
                }
                return emit_enum_constructor(
                    expression.type.name,
                    expression.value.substr(separator + 2), {});
            }
            if (expression.value == "(*this)") {
                if (current_owner_type_.empty()) {
                    unsupported("self outside a custom method");
                }
                return {Type{Type::Kind::custom, current_owner_type_}, "%sun.this"};
            }
            const auto found = variables_.find(expression.value);
            if (found == variables_.end()) {
                unsupported("variable '" + expression.value + "'");
            }
            const auto result = temporary();
            out_ << "  " << result << " = load " << llvm_type(found->second)
                 << ", " << slot_pointer(expression.value) << '\n';
            if (is_string_like(found->second)) {
                const auto length = temporary();
                out_ << "  " << length << " = load i64, "
                     << length_pointer(expression.value) << '\n';
                return {found->second, result, length};
            }
            return {found->second, result};
        }
        if (expression.kind == Expression::Kind::member) {
            const auto object = emit_expression(expression.children.at(0));
            const auto pointer = emit_field_pointer(object, expression.value);
            if (is_string_like(expression.type)) {
                const auto data_pointer = temporary();
                const auto length_pointer = temporary();
                const auto data = temporary();
                const auto length = temporary();
                out_ << "  " << data_pointer
                     << " = getelementptr { ptr, i64 }, ptr " << pointer
                     << ", i32 0, i32 0\n"
                     << "  " << length_pointer
                     << " = getelementptr { ptr, i64 }, ptr " << pointer
                     << ", i32 0, i32 1\n"
                     << "  " << data << " = load ptr, ptr " << data_pointer << '\n'
                     << "  " << length << " = load i64, ptr " << length_pointer
                     << '\n';
                return {expression.type, data, length};
            }
            const auto result = temporary();
            out_ << "  " << result << " = load " << llvm_type(expression.type)
                 << ", ptr " << pointer << '\n';
            return {expression.type, result};
        }
        if (expression.kind == Expression::Kind::cast) {
            const auto value = emit_expression(expression.children.at(0));
            if (expression.value == "double" && value.type == Type::integer) {
                const auto result = temporary();
                out_ << "  " << result << " = sitofp i64 " << value.operand
                     << " to double\n";
                return {Type::number, result};
            }
            if (expression.value == "std::int64_t" && value.type == Type::integer) {
                return {Type::integer, value.operand};
            }
            if (expression.value == "std::int64_t" && value.type == Type::number) {
                const auto result = temporary();
                out_ << "  " << result << " = call i64 @sun_llvm_checked_number_to_int(double "
                     << value.operand << ")\n";
                return {Type::integer, result};
            }
            unsupported("cast to '" + expression.value + "'");
        }
        if (expression.kind == Expression::Kind::member_call) {
            if (!expression.children.empty() &&
                expression.children[0].type.kind == Type::Kind::custom &&
                custom_types_.count(expression.children[0].type.name) &&
                !custom_types_.at(expression.children[0].type.name)->is_enum) {
                const auto* method = find_method(expression.children[0].type.name,
                                                 expression.value);
                if (!method) unsupported("method '" + expression.value + "'");
                const auto receiver = emit_expression(expression.children[0]);
                std::vector<Expression> arguments(
                    expression.children.begin() + 1, expression.children.end());
                std::ostringstream call;
                call << "ptr " << receiver.operand;
                if (!arguments.empty()) call << ", ";
                emit_call_arguments(call, arguments, *method);
                std::string length_slot;
                if (method->return_type &&
                    is_string_like(*method->return_type)) {
                    length_slot = temporary();
                    out_ << "  " << length_slot << " = alloca i64\n";
                    if (!call.str().empty()) call << ", ";
                    call << "ptr " << length_slot;
                }
                const auto result = temporary();
                out_ << "  " << result << " = call "
                     << (method->return_type ? llvm_type(*method->return_type) : "void")
                     << " @sun_method_" << method->owner_type << '_'
                     << method->name << '(' << call.str() << ")\n";
                if (!method->return_type) {
                    unsupported("void method used as a value");
                }
                if (is_string_like(*method->return_type)) {
                    const auto length = temporary();
                    out_ << "  " << length << " = load i64, ptr " << length_slot
                         << '\n';
                    return {*method->return_type, result, length};
                }
                return {*method->return_type, result};
            }
            if ((expression.value == "count" || expression.value == "index") &&
                expression.children.size() == 2 &&
                expression.children[0].type.kind == Type::Kind::tuple) {
                const auto tuple = emit_expression(expression.children[0]);
                const auto needle = emit_expression(expression.children[1]);
                const auto items = llvm_tuple_types(tuple.type);
                std::vector<std::string> matches(items.size(), "false");
                for (std::size_t i = 0; i < items.size(); ++i) {
                    if (items[i] == needle.type ||
                        (is_string_like(items[i]) && is_string_like(needle.type))) {
                        matches[i] = emit_equal_values(
                            items[i], emit_tuple_item(tuple, i), needle);
                    }
                }
                if (expression.value == "count") {
                    std::string count = "0";
                    for (const auto& match : matches) {
                        const auto increment = temporary();
                        const auto next = temporary();
                        out_ << "  " << increment << " = zext i1 " << match
                             << " to i64\n"
                             << "  " << next << " = add i64 " << count
                             << ", " << increment << '\n';
                        count = next;
                    }
                    return {Type::integer, count};
                }
                std::string found = "-1";
                for (std::size_t i = matches.size(); i > 0; --i) {
                    const auto selected = temporary();
                    out_ << "  " << selected << " = select i1 " << matches[i - 1]
                         << ", i64 " << (i - 1) << ", i64 " << found << '\n';
                    found = selected;
                }
                const auto missing = temporary();
                const auto fail_label = label("tuple.index.missing");
                const auto found_label = label("tuple.index.found");
                out_ << "  " << missing << " = icmp eq i64 " << found << ", -1\n"
                     << "  br i1 " << missing << ", label %" << fail_label
                     << ", label %" << found_label << '\n'
                     << fail_label << ":\n"
                     << "  call void @sun_llvm_tuple_index_missing()\n"
                     << "  unreachable\n"
                     << found_label << ":\n";
                return {Type::integer, found};
            }
            if (expression.value == "size" && expression.children.size() == 1) {
                const auto receiver = emit_expression(expression.children[0]);
                if (is_list_type(expression.children[0].type)) {
                    const auto suffix = llvm_list_suffix(
                        list_item_type(expression.children[0].type));
                    const auto result = temporary();
                    out_ << "  " << result << " = call i64 @sun_llvm_list_len_" << suffix
                         << "(ptr " << receiver.operand << ")\n";
                    return {Type::integer, result};
                }
                if (expression.children[0].type == Type::bytes) {
                    const auto result = temporary();
                    out_ << "  " << result << " = call i64 @sun_llvm_bytes_len(ptr "
                         << receiver.operand << ")\n";
                    return {Type::integer, result};
                }
                if (is_dictionary_type(expression.children[0].type)) {
                    const auto result = temporary();
                    out_ << "  " << result << " = call i64 @sun_llvm_dict_len(ptr "
                         << receiver.operand << ")\n";
                    return {Type::integer, result};
                }
                if (expression.children[0].type == Type::string) {
                    return {Type::integer, receiver.string_length};
                }
            }
            unsupported("member call '" + expression.value + "'");
        }
        if (expression.kind == Expression::Kind::unary) {
            const auto value = emit_expression(expression.children.at(0));
            const auto result = temporary();
            if (expression.value == "!" && value.type == Type::boolean) {
                out_ << "  " << result << " = xor i1 " << value.operand
                     << ", true\n";
                return {Type::boolean, result};
            }
            if (expression.value == "-" && value.type == Type::number) {
                out_ << "  " << result << " = fneg double " << value.operand << '\n';
                return {Type::number, result};
            }
            if (expression.value == "sun_int_neg" && value.type == Type::integer) {
                out_ << "  " << result << " = call i64 @sun_llvm_checked_neg(i64 "
                     << value.operand << ")\n";
                return {Type::integer, result};
            }
            unsupported("unary operator '" + expression.value + "'");
        }
        if (expression.kind == Expression::Kind::binary) {
            return emit_binary(expression);
        }
        if (expression.kind == Expression::Kind::call) {
            if (expression.value == "sun_io_open" &&
                expression.children.size() == 2) {
                const auto path = emit_expression(expression.children[0]);
                const auto mode = emit_expression(expression.children[1]);
                const auto result = temporary();
                out_ << "  " << result << " = call i64 @sun_llvm_io_open(ptr "
                     << path.operand << ", i64 " << path.string_length << ", ptr "
                     << mode.operand << ", i64 " << mode.string_length << ")\n";
                return {Type::integer, result};
            }
            if (expression.value == "sun_io_read" &&
                expression.children.size() == 1) {
                const auto handle = emit_expression(expression.children[0]);
                const auto length_slot = temporary();
                const auto result = temporary();
                out_ << "  " << length_slot << " = alloca i64\n"
                     << "  " << result << " = call ptr @sun_llvm_io_read(i64 "
                     << handle.operand << ", ptr " << length_slot << ")\n";
                const auto length = temporary();
                out_ << "  " << length << " = load i64, ptr " << length_slot << '\n';
                return {Type::string, result, length};
            }
            if (expression.type.kind == Type::Kind::custom &&
                expression.value.rfind("sun_make_", 0) == 0) {
                return emit_custom_constructor(
                    expression.type.name, expression.children);
            }
            if (expression.value.rfind("std::get<", 0) == 0 &&
                expression.value.back() == '>' && expression.children.size() == 1 &&
                expression.children[0].type.kind == Type::Kind::tuple) {
                const auto index_text = expression.value.substr(
                    std::string("std::get<").size(),
                    expression.value.size() - std::string("std::get<").size() - 1);
                const auto index = static_cast<std::size_t>(std::stoul(index_text));
                const auto tuple = emit_expression(expression.children[0]);
                return emit_tuple_item(tuple, index);
            }
            if (expression.type.kind == Type::Kind::custom &&
                custom_error_types_.count(expression.type.name) &&
                expression.value == "SunType_" + expression.type.name) {
                if (expression.children.size() != 1 ||
                    expression.children.front().type != Type::string) {
                    unsupported("custom error constructors must take only a string message");
                }
                const auto message = emit_expression(expression.children.front());
                const auto error_type = string_pointer(expression.type.name);
                const auto result = temporary();
                out_ << "  " << result << " = call ptr @sun_llvm_error_create(ptr "
                     << error_type << ", i64 " << expression.type.name.size()
                     << ", ptr " << message.operand << ", i64 "
                     << message.string_length << ")\n";
                return {expression.type, result};
            }
            const auto function = functions_.find(expression.value);
            if (function != functions_.end()) {
                std::ostringstream arguments;
                emit_call_arguments(arguments, expression.children, *function->second);
                emit_captured_arguments(arguments, expression.value);
                const auto result = temporary();
                auto argument_text = arguments.str();
                std::string length_slot;
                if (function->second->return_type &&
                    is_string_like(*function->second->return_type)) {
                    length_slot = temporary();
                    out_ << "  " << length_slot << " = alloca i64\n";
                    if (!argument_text.empty()) argument_text += ", ";
                    argument_text += "ptr " + length_slot;
                }
                out_ << "  " << result << " = call " << llvm_type(expression.type)
                     << " @" << expression.value << '(' << argument_text << ")\n";
                if (is_string_like(expression.type)) {
                    const auto length = temporary();
                    out_ << "  " << length << " = load i64, ptr " << length_slot << '\n';
                    return {expression.type, result, length};
                }
                return {expression.type, result};
            }
            if (expression.value == "sun_input_string" ||
                expression.value == "sun_input_char" ||
                expression.value == "sun_input_number" ||
                expression.value == "sun_input_int" ||
                expression.value == "sun_input_bool") {
                const auto prompt = emit_expression(expression.children.at(0));
                const auto result = temporary();
                if (expression.value == "sun_input_string" ||
                    expression.value == "sun_input_char") {
                    const auto length_slot = temporary();
                    out_ << "  " << length_slot << " = alloca i64\n"
                         << "  " << result << " = call ptr @sun_llvm_input_"
                         << (expression.value == "sun_input_string" ? "string" : "char")
                         << "(ptr " << prompt.operand << ", i64 "
                         << prompt.string_length << ", ptr " << length_slot << ")\n";
                    const auto length = temporary();
                    out_ << "  " << length << " = load i64, ptr " << length_slot << '\n';
                    return {expression.type, result, length};
                }
                const auto type = expression.value == "sun_input_number" ? "double"
                                : expression.value == "sun_input_int" ? "i64" : "i1";
                out_ << "  " << result << " = call " << type << " @sun_llvm_input_"
                     << (expression.value == "sun_input_number" ? "number"
                         : expression.value == "sun_input_int" ? "int" : "bool")
                     << "(ptr " << prompt.operand << ", i64 "
                     << prompt.string_length << ")\n";
                return {expression.type, result};
            }
            if ((expression.value == "sun_sum_int" ||
                 expression.value == "sun_sum_number" ||
                 expression.value == "sun_all" ||
                 expression.value == "sun_any") &&
                expression.children.size() == 1) {
                const auto list = emit_expression(expression.children[0]);
                const auto helper = expression.value == "sun_sum_int"
                                        ? "sun_llvm_list_sum_i64"
                                    : expression.value == "sun_sum_number"
                                        ? "sun_llvm_list_sum_double"
                                    : expression.value == "sun_all"
                                        ? "sun_llvm_list_all_bool"
                                        : "sun_llvm_list_any_bool";
                const auto result = temporary();
                out_ << "  " << result << " = call " << llvm_type(expression.type)
                     << " @" << helper << "(ptr " << list.operand << ")\n";
                return {expression.type, result};
            }
            if (expression.value == "sun_round_digits" &&
                expression.children.size() == 2) {
                const auto value = emit_expression(expression.children[0]);
                const auto digits = emit_expression(expression.children[1]);
                const auto result = temporary();
                out_ << "  " << result << " = call double @sun_llvm_round_digits(double "
                     << value.operand << ", i64 " << digits.operand << ")\n";
                return {Type::number, result};
            }
            if (expression.value == "sun_math_factorial" &&
                expression.children.size() == 1) {
                const auto value = emit_expression(expression.children[0]);
                const auto result = temporary();
                out_ << "  " << result << " = call double @sun_llvm_math_factorial(double "
                     << value.operand << ")\n";
                return {Type::number, result};
            }
            if ((expression.value == "sun_min" || expression.value == "sun_max") &&
                expression.children.size() == 2) {
                const auto left = emit_expression(expression.children[0]);
                const auto right = emit_expression(expression.children[1]);
                const auto result = temporary();
                if (left.type == Type::integer) {
                    const auto predicate = expression.value == "sun_min" ? "sle" : "sge";
                    const auto choose_left = temporary();
                    out_ << "  " << choose_left << " = icmp " << predicate << " i64 "
                         << left.operand << ", " << right.operand << '\n'
                         << "  " << result << " = select i1 " << choose_left
                         << ", i64 " << left.operand << ", i64 " << right.operand << '\n';
                } else {
                    const auto is_nan = temporary();
                    const auto ordered = temporary();
                    const auto selected = temporary();
                    const auto predicate = expression.value == "sun_min" ? "ole" : "oge";
                    out_ << "  " << is_nan << " = fcmp uno double " << left.operand
                         << ", " << right.operand << '\n'
                         << "  " << ordered << " = fcmp " << predicate << " double "
                         << left.operand << ", " << right.operand << '\n'
                         << "  " << selected << " = select i1 " << ordered
                         << ", double " << left.operand << ", double " << right.operand
                         << '\n'
                         << "  " << result << " = select i1 " << is_nan << ", double "
                         << llvm_double(std::numeric_limits<double>::quiet_NaN())
                         << ", double " << selected << '\n';
                }
                return {expression.type, result};
            }
            if (expression.value == "std::abs" || expression.value == "std::fabs" ||
                expression.value == "std::floor" || expression.value == "std::ceil" ||
                expression.value == "std::trunc" || expression.value == "std::round") {
                const auto value = emit_expression(expression.children.at(0));
                const auto intrinsic = expression.value == "std::abs" ||
                                               expression.value == "std::fabs"
                                           ? "llvm.fabs.f64"
                                       : expression.value == "std::floor"
                                           ? "llvm.floor.f64"
                                       : expression.value == "std::ceil"
                                           ? "llvm.ceil.f64"
                                       : expression.value == "std::trunc"
                                           ? "llvm.trunc.f64"
                                           : "llvm.round.f64";
                const auto result = temporary();
                out_ << "  " << result << " = call double @" << intrinsic
                     << "(double " << value.operand << ")\n";
                return {Type::number, result};
            }
            if (expression.value.rfind("std::", 0) == 0) {
                const auto function_name =
                    "sun_llvm_math_" + expression.value.substr(std::string("std::").size());
                std::vector<Value> arguments;
                arguments.reserve(expression.children.size());
                for (const auto& child : expression.children) {
                    arguments.push_back(emit_expression(child));
                }
                const auto result = temporary();
                out_ << "  " << result << " = call double @" << function_name << '(';
                for (std::size_t i = 0; i < arguments.size(); ++i) {
                    if (i) out_ << ", ";
                    out_ << "double " << arguments[i].operand;
                }
                out_ << ")\n";
                return {Type::number, result};
            }
            if (expression.value.rfind("sun_string_", 0) == 0 &&
                !expression.children.empty()) {
                const auto value = emit_expression(expression.children[0]);
                const auto result = temporary();
                if (expression.value == "sun_string_upper" ||
                    expression.value == "sun_string_lower" ||
                    expression.value == "sun_string_trim") {
                    const auto length_slot = temporary();
                    out_ << "  " << length_slot << " = alloca i64\n"
                         << "  " << result << " = call ptr @sun_llvm_string_"
                         << expression.value.substr(std::string("sun_string_").size())
                         << "(ptr " << value.operand << ", i64 "
                         << value.string_length << ", ptr " << length_slot << ")\n";
                    const auto length = temporary();
                    out_ << "  " << length << " = load i64, ptr " << length_slot << '\n';
                    return {expression.type, result, length};
                }
                if (expression.value == "sun_string_split") {
                    const auto separator = emit_expression(expression.children.at(1));
                    out_ << "  " << result << " = call ptr @sun_llvm_string_split(ptr "
                         << value.operand << ", i64 " << value.string_length
                         << ", ptr " << separator.operand << ", i64 "
                         << separator.string_length << ")\n";
                    return {expression.type, result};
                }
                if (expression.value == "sun_string_replace") {
                    const auto search = emit_expression(expression.children.at(1));
                    const auto replacement = emit_expression(expression.children.at(2));
                    const auto length_slot = temporary();
                    out_ << "  " << length_slot << " = alloca i64\n"
                         << "  " << result
                         << " = call ptr @sun_llvm_string_replace(ptr "
                         << value.operand << ", i64 " << value.string_length
                         << ", ptr " << search.operand << ", i64 "
                         << search.string_length << ", ptr " << replacement.operand
                         << ", i64 " << replacement.string_length << ", ptr "
                         << length_slot << ")\n";
                    const auto length = temporary();
                    out_ << "  " << length << " = load i64, ptr " << length_slot << '\n';
                    return {expression.type, result, length};
                }
                if (expression.value == "sun_string_contains" ||
                    expression.value == "sun_string_count" ||
                    expression.value == "sun_string_starts_with" ||
                    expression.value == "sun_string_ends_with") {
                    const auto part = emit_expression(expression.children.at(1));
                    const auto operation = expression.value.substr(
                        std::string("sun_string_").size());
                    out_ << "  " << result << " = call "
                         << (expression.type == Type::integer ? "i64" : "i1")
                         << " @sun_llvm_string_" << operation << "(ptr "
                         << value.operand << ", i64 " << value.string_length
                         << ", ptr " << part.operand << ", i64 "
                         << part.string_length << ")\n";
                    return {expression.type, result};
                }
            }
            if (expression.value == "sun_list_at" && expression.children.size() == 2) {
                const auto list = emit_expression(expression.children[0]);
                auto index = emit_expression(expression.children[1]);
                if (index.type == Type::integer) {
                    const auto converted = temporary();
                    out_ << "  " << converted << " = sitofp i64 " << index.operand << " to double\n";
                    index = {Type::number, converted};
                }
                const auto suffix = llvm_list_suffix(expression.type);
                if (is_string_like(expression.type)) {
                    const auto length_slot = temporary();
                    out_ << "  " << length_slot << " = alloca i64\n";
                    const auto result = temporary();
                    out_ << "  " << result << " = call ptr @sun_llvm_list_at_string(ptr "
                         << list.operand << ", double " << index.operand << ", ptr "
                         << length_slot << ")\n";
                    const auto length = temporary();
                    out_ << "  " << length << " = load i64, ptr " << length_slot << '\n';
                    return {expression.type, result, length};
                }
                if (expression.type.kind == Type::Kind::tuple) {
                    const auto pointer = temporary();
                    out_ << "  " << pointer << " = call ptr @sun_llvm_list_at_ptr(ptr "
                         << list.operand << ", double " << index.operand << ")\n";
                    return unbox_collection_item(expression.type, pointer);
                }
                const auto result = temporary();
                out_ << "  " << result << " = call " << llvm_type(expression.type)
                     << " @sun_llvm_list_at_" << suffix << "(ptr " << list.operand
                     << ", double " << index.operand << ")\n";
                return {expression.type, result};
            }
            if ((expression.value == "sun_list_contains" ||
                 expression.value == "sun_list_remove" ||
                 expression.value == "sun_list_pop") &&
                !expression.children.empty()) {
                const auto list = emit_expression(expression.children[0]);
                if (expression.value == "sun_list_contains") {
                    const auto item = emit_expression(expression.children[1]);
                    const auto suffix = llvm_list_suffix(item.type);
                    const auto result = temporary();
                    if (is_string_like(item.type)) {
                        out_ << "  " << result << " = call i1 @sun_llvm_list_contains_string(ptr "
                             << list.operand << ", ptr " << item.operand << ", i64 "
                             << item.string_length << ")\n";
                    } else if (item.type.kind == Type::Kind::tuple) {
                        const auto boxed = box_collection_item(item);
                        out_ << "  " << result << " = call i1 @sun_llvm_list_contains_ptr(ptr "
                             << list.operand << ", ptr "
                             << boxed << ")\n";
                    } else {
                        out_ << "  " << result << " = call i1 @sun_llvm_list_contains_" << suffix
                             << "(ptr " << list.operand << ", " << llvm_type(item.type) << " "
                             << item.operand << ")\n";
                    }
                    return {Type::boolean, result};
                }
                if (expression.value == "sun_list_remove") {
                    const auto item = emit_expression(expression.children[1]);
                    const auto suffix = llvm_list_suffix(item.type);
                    const auto result = temporary();
                    if (is_string_like(item.type)) {
                        out_ << "  " << result << " = call i1 @sun_llvm_list_remove_string(ptr "
                             << list.operand << ", ptr " << item.operand << ", i64 "
                             << item.string_length << ")\n";
                    } else if (item.type.kind == Type::Kind::tuple) {
                        const auto boxed = box_collection_item(item);
                        out_ << "  " << result << " = call i1 @sun_llvm_list_remove_ptr(ptr "
                             << list.operand << ", ptr "
                             << boxed << ")\n";
                    } else {
                        out_ << "  " << result << " = call i1 @sun_llvm_list_remove_" << suffix
                             << "(ptr " << list.operand << ", " << llvm_type(item.type) << " "
                             << item.operand << ")\n";
                    }
                    return {Type::boolean, result};
                }
                auto index = emit_expression(expression.children[1]);
                if (index.type == Type::integer) {
                    const auto converted = temporary();
                    out_ << "  " << converted << " = sitofp i64 " << index.operand << " to double\n";
                    index = {Type::number, converted};
                }
                const auto suffix = llvm_list_suffix(expression.type);
                if (is_string_like(expression.type)) {
                    const auto length_slot = temporary();
                    out_ << "  " << length_slot << " = alloca i64\n";
                    const auto result = temporary();
                    out_ << "  " << result << " = call ptr @sun_llvm_list_pop_string(ptr "
                         << list.operand << ", double " << index.operand << ", ptr "
                         << length_slot << ")\n";
                    const auto length = temporary();
                    out_ << "  " << length << " = load i64, ptr " << length_slot << '\n';
                    return {expression.type, result, length};
                }
                if (expression.type.kind == Type::Kind::tuple) {
                    const auto result = temporary();
                    out_ << "  " << result << " = call ptr @sun_llvm_list_pop_ptr(ptr "
                         << list.operand << ", double " << index.operand << ")\n";
                    return unbox_collection_item(expression.type, result);
                }
                const auto result = temporary();
                out_ << "  " << result << " = call " << llvm_type(expression.type)
                     << " @sun_llvm_list_pop_" << suffix << "(ptr " << list.operand
                     << ", double " << index.operand << ")\n";
                return {expression.type, result};
            }
            if (expression.kind == Expression::Kind::member_call &&
                expression.value == "size" && expression.children.size() == 1) {
                const auto receiver = emit_expression(expression.children[0]);
                const auto receiver_type = expression.children[0].type;
                if (is_dictionary_type(receiver_type)) {
                    const auto result = temporary();
                    out_ << "  " << result << " = call i64 @sun_llvm_dict_len(ptr "
                         << receiver.operand << ")\n";
                    return {Type::integer, result};
                }
                if (receiver_type == Type::bytes) {
                    const auto result = temporary();
                    out_ << "  " << result << " = call i64 @sun_llvm_bytes_len(ptr "
                         << receiver.operand << ")\n";
                    return {Type::integer, result};
                }
                if (is_list_type(receiver_type)) {
                    const auto suffix = llvm_list_suffix(list_item_type(receiver_type));
                    const auto result = temporary();
                    out_ << "  " << result << " = call i64 @sun_llvm_list_len_" << suffix
                         << "(ptr " << receiver.operand << ")\n";
                    return {Type::integer, result};
                }
            }
            if (expression.value == "sun_dict_at" && expression.children.size() == 2) {
                const auto dict = emit_expression(expression.children[0]);
                const auto key = emit_expression(expression.children[1]);
                const auto value_type = expression.type;
                const auto key_string = key.type == Type::string || key.type == Type::character;
                if (key_string) {
                    if (value_type == Type::integer) {
                        const auto result = temporary();
                        out_ << "  " << result << " = call i64 @sun_llvm_dict_at_string_i64(ptr " << dict.operand << ", ptr " << key.operand << ", i64 " << key.string_length << ")\n";
                        return {value_type, result};
                    }
                    if (value_type == Type::number) {
                        const auto result = temporary();
                        out_ << "  " << result << " = call double @sun_llvm_dict_at_string_double(ptr " << dict.operand << ", ptr " << key.operand << ", i64 " << key.string_length << ")\n";
                        return {value_type, result};
                    }
                    if (value_type == Type::boolean) {
                        const auto result = temporary();
                        out_ << "  " << result << " = call i1 @sun_llvm_dict_at_string_bool(ptr " << dict.operand << ", ptr " << key.operand << ", i64 " << key.string_length << ")\n";
                        return {value_type, result};
                    }
                    if (is_string_like(value_type)) {
                        const auto length_slot = temporary();
                        out_ << "  " << length_slot << " = alloca i64\n";
                        const auto result = temporary();
                        out_ << "  " << result << " = call ptr @sun_llvm_dict_at_string_string(ptr " << dict.operand << ", ptr " << key.operand << ", i64 " << key.string_length << ", ptr " << length_slot << ")\n";
                        const auto length = temporary();
                        out_ << "  " << length << " = load i64, ptr " << length_slot << '\n';
                        return {value_type, result, length};
                    }
                    if (llvm_type(value_type) == "ptr" ||
                        value_type.kind == Type::Kind::tuple) {
                        const auto result = temporary();
                        out_ << "  " << result << " = call ptr @sun_llvm_dict_at_string_ptr(ptr "
                             << dict.operand << ", ptr " << key.operand << ", i64 "
                             << key.string_length << ")\n";
                        return unbox_collection_item(value_type, result);
                    }
                } else if (key.type == Type::integer) {
                    if (value_type == Type::integer) {
                        const auto result = temporary();
                        out_ << "  " << result << " = call i64 @sun_llvm_dict_at_i64_i64(ptr " << dict.operand << ", i64 " << key.operand << ")\n";
                        return {value_type, result};
                    }
                    if (value_type == Type::number) {
                        const auto result = temporary();
                        out_ << "  " << result << " = call double @sun_llvm_dict_at_i64_double(ptr " << dict.operand << ", i64 " << key.operand << ")\n";
                        return {value_type, result};
                    }
                    if (value_type == Type::boolean) {
                        const auto result = temporary();
                        out_ << "  " << result << " = call i1 @sun_llvm_dict_at_i64_bool(ptr " << dict.operand << ", i64 " << key.operand << ")\n";
                        return {value_type, result};
                    }
                    if (is_string_like(value_type)) {
                        const auto length_slot = temporary();
                        out_ << "  " << length_slot << " = alloca i64\n";
                        const auto result = temporary();
                        out_ << "  " << result << " = call ptr @sun_llvm_dict_at_i64_string(ptr " << dict.operand << ", i64 " << key.operand << ", ptr " << length_slot << ")\n";
                        const auto length = temporary();
                        out_ << "  " << length << " = load i64, ptr " << length_slot << '\n';
                        return {value_type, result, length};
                    }
                    if (llvm_type(value_type) == "ptr" ||
                        value_type.kind == Type::Kind::tuple) {
                        const auto result = temporary();
                        out_ << "  " << result << " = call ptr @sun_llvm_dict_at_i64_ptr(ptr "
                             << dict.operand << ", i64 " << key.operand << ")\n";
                        return unbox_collection_item(value_type, result);
                    }
                } else if (key.type == Type::boolean) {
                    if (value_type == Type::integer) {
                        const auto result = temporary();
                        out_ << "  " << result << " = call i64 @sun_llvm_dict_at_bool_i64(ptr " << dict.operand << ", i1 " << key.operand << ")\n";
                        return {value_type, result};
                    }
                    if (value_type == Type::number) {
                        const auto result = temporary();
                        out_ << "  " << result << " = call double @sun_llvm_dict_at_bool_double(ptr " << dict.operand << ", i1 " << key.operand << ")\n";
                        return {value_type, result};
                    }
                    if (value_type == Type::boolean) {
                        const auto result = temporary();
                        out_ << "  " << result << " = call i1 @sun_llvm_dict_at_bool_bool(ptr " << dict.operand << ", i1 " << key.operand << ")\n";
                        return {value_type, result};
                    }
                    if (is_string_like(value_type)) {
                        const auto length_slot = temporary();
                        out_ << "  " << length_slot << " = alloca i64\n";
                        const auto result = temporary();
                        out_ << "  " << result << " = call ptr @sun_llvm_dict_at_bool_string(ptr " << dict.operand << ", i1 " << key.operand << ", ptr " << length_slot << ")\n";
                        const auto length = temporary();
                        out_ << "  " << length << " = load i64, ptr " << length_slot << '\n';
                        return {value_type, result, length};
                    }
                    if (llvm_type(value_type) == "ptr" ||
                        value_type.kind == Type::Kind::tuple) {
                        const auto result = temporary();
                        out_ << "  " << result << " = call ptr @sun_llvm_dict_at_bool_ptr(ptr "
                             << dict.operand << ", i1 " << key.operand << ")\n";
                        return unbox_collection_item(value_type, result);
                    }
                }
                unsupported("dictionary access type");
            }
            if ((expression.value == "sun_dict_keys" ||
                 expression.value == "sun_dict_values") &&
                expression.children.size() == 1) {
                const auto dict = emit_expression(expression.children[0]);
                const auto item_type = expression.value == "sun_dict_keys"
                                           ? dictionary_key_type(expression.children[0].type)
                                           : dictionary_value_type(expression.children[0].type);
                const auto suffix = llvm_list_suffix(item_type);
                const auto result = temporary();
                out_ << "  " << result << " = call ptr @sun_llvm_dict_"
                     << (expression.value == "sun_dict_keys" ? "keys_" : "values_")
                     << suffix << "(ptr " << dict.operand << ")\n";
                return {expression.type, result};
            }
            if (expression.value == "sun_dict_get" && expression.children.size() == 3) {
                const auto dict = emit_expression(expression.children[0]);
                const auto key = emit_expression(expression.children[1]);
                const auto fallback = emit_expression(expression.children[2]);
                const auto key_suffix = is_string_like(key.type) ? "string"
                                        : key.type == Type::boolean ? "bool" : "i64";
                const auto value_suffix = expression.type == Type::integer ? "i64"
                                        : expression.type == Type::number ? "double"
                                        : expression.type == Type::boolean ? "bool"
                                        : is_string_like(expression.type) ? "string"
                                        : "ptr";
                const auto result = temporary();
                std::ostringstream args;
                args << "ptr " << dict.operand << ", ";
                if (key_suffix == std::string("string")) {
                    args << "ptr " << key.operand << ", i64 " << key.string_length;
                } else if (key_suffix == std::string("bool")) {
                    args << "i1 " << key.operand;
                } else {
                    args << "i64 " << key.operand;
                }
                if (is_string_like(expression.type)) {
                    const auto length_slot = temporary();
                    out_ << "  " << length_slot << " = alloca i64\n";
                    args << ", ptr " << fallback.operand << ", i64 "
                         << fallback.string_length << ", ptr " << length_slot;
                    out_ << "  " << result << " = call ptr @sun_llvm_dict_get_"
                         << key_suffix << "_string(" << args.str() << ")\n";
                    const auto length = temporary();
                    out_ << "  " << length << " = load i64, ptr " << length_slot << '\n';
                    return {expression.type, result, length};
                }
                if (llvm_type(expression.type) == "ptr" ||
                    expression.type.kind == Type::Kind::tuple) {
                    args << ", ptr " << box_collection_item(fallback);
                    out_ << "  " << result << " = call ptr @sun_llvm_dict_get_"
                         << key_suffix << "_ptr(" << args.str() << ")\n";
                    return unbox_collection_item(expression.type, result);
                }
                args << ", " << llvm_type(fallback.type) << ' ' << fallback.operand;
                out_ << "  " << result << " = call " << llvm_type(expression.type)
                     << " @sun_llvm_dict_get_" << key_suffix << '_' << value_suffix
                     << '(' << args.str() << ")\n";
                return {expression.type, result};
            }
            if (expression.value == "sun_dict_contains" || expression.value == "sun_dict_remove") {
                const auto dict = emit_expression(expression.children[0]);
                const auto key = emit_expression(expression.children[1]);
                const bool key_string = key.type == Type::string || key.type == Type::character;
                const auto result = temporary();
                const auto operation = expression.value == "sun_dict_contains" ? "contains" : "remove";
                if (key_string) out_ << "  " << result << " = call i1 @sun_llvm_dict_" << operation << "_string(ptr " << dict.operand << ", ptr " << key.operand << ", i64 " << key.string_length << ")\n";
                else if (key.type == Type::boolean) out_ << "  " << result << " = call i1 @sun_llvm_dict_" << operation << "_bool(ptr " << dict.operand << ", i1 " << key.operand << ")\n";
                else out_ << "  " << result << " = call i1 @sun_llvm_dict_" << operation << "_i64(ptr " << dict.operand << ", i64 " << key.operand << ")\n";
                return {Type::boolean, result};
            }
            if (expression.value == "sun_bytes_from_string" && expression.children.size() == 1) {
                const auto value = emit_expression(expression.children[0]);
                const auto result = temporary();
                out_ << "  " << result << " = call ptr @sun_llvm_bytes_from_string(ptr "
                     << value.operand << ", i64 " << value.string_length << ")\n";
                return {Type::bytes, result};
            }
            if (expression.value == "sun_bytes_from_list" && expression.children.size() == 1) {
                if (expression.children[0].kind == Expression::Kind::empty_list) {
                    const auto result = temporary();
                    out_ << "  " << result << " = call ptr @sun_llvm_bytes_create()\n";
                    return {Type::bytes, result};
                }
                const auto list = emit_expression(expression.children[0]);
                const auto result = temporary();
                out_ << "  " << result << " = call ptr @sun_llvm_bytes_from_list(ptr "
                     << list.operand << ")\n";
                return {Type::bytes, result};
            }
            if (expression.value == "sun_bytes_to_list" && expression.children.size() == 1) {
                const auto bytes = emit_expression(expression.children[0]);
                const auto result = temporary();
                out_ << "  " << result << " = call ptr @sun_llvm_bytes_to_list(ptr "
                     << bytes.operand << ")\n";
                return {Type::list_integer, result};
            }
            if (expression.value == "sun_bytes_to_string" && expression.children.size() == 1) {
                const auto value = emit_expression(expression.children[0]);
                const auto length_slot = temporary();
                out_ << "  " << length_slot << " = alloca i64\n";
                const auto result = temporary();
                out_ << "  " << result << " = call ptr @sun_llvm_bytes_to_string(ptr "
                     << value.operand << ", ptr " << length_slot << ")\n";
                const auto length = temporary();
                out_ << "  " << length << " = load i64, ptr " << length_slot << '\n';
                return {Type::string, result, length};
            }
            if (expression.value == "sun_bytes_at" && expression.children.size() == 2) {
                const auto bytes = emit_expression(expression.children[0]);
                auto index = emit_expression(expression.children[1]);
                if (index.type == Type::integer) {
                    const auto converted = temporary();
                    out_ << "  " << converted << " = sitofp i64 " << index.operand << " to double\n";
                    index = {Type::number, converted};
                }
                const auto result = temporary();
                out_ << "  " << result << " = call i64 @sun_llvm_bytes_at(ptr "
                     << bytes.operand << ", double " << index.operand << ")\n";
                return {Type::integer, result};
            }
            if (expression.value == "sun_bytes_contains" && expression.children.size() == 2) {
                const auto bytes = emit_expression(expression.children[0]);
                const auto value = emit_expression(expression.children[1]);
                 const auto integer = temporary();
                 out_ << "  " << integer
                     << " = call i64 @sun_llvm_checked_number_to_int(double "
                     << value.operand << ")\n";
                const auto result = temporary();
                out_ << "  " << result << " = call i1 @sun_llvm_bytes_contains(ptr "
                     << bytes.operand << ", i64 " << integer << ")\n";
                return {Type::boolean, result};
            }
            if (expression.value == "sun_to_char" && expression.children.size() == 1) {
                const auto value = emit_expression(expression.children.front());
                const auto result = temporary();
                out_ << "  " << result << " = call ptr @sun_llvm_checked_char(ptr "
                     << value.operand << ", i64 " << value.string_length << ")\n";
                return {Type::character, result, value.string_length};
            }
            if (expression.value == "sun_to_int" && expression.children.size() == 1) {
                const auto value = emit_expression(expression.children[0]);
                if (value.type == Type::integer) {
                    return {Type::integer, value.operand};
                }
                if (value.type == Type::number) {
                    const auto result = temporary();
                    out_ << "  " << result << " = call i64 @sun_llvm_checked_number_to_int(double "
                         << value.operand << ")\n";
                    return {Type::integer, result};
                }
                unsupported("int conversion source type '" + cpp_type(value.type) + "'");
            }
            if (expression.value == "sun_int_add" ||
                expression.value == "sun_int_sub" ||
                expression.value == "sun_int_mul" ||
                expression.value == "sun_int_div" ||
                expression.value == "sun_int_mod") {
                const auto left = emit_expression(expression.children.at(0));
                const auto right = emit_expression(expression.children.at(1));
                const auto helper = expression.value == "sun_int_add" ? "sun_llvm_checked_add"
                                  : expression.value == "sun_int_sub" ? "sun_llvm_checked_sub"
                                  : expression.value == "sun_int_mul" ? "sun_llvm_checked_mul"
                                  : expression.value == "sun_int_div" ? "sun_llvm_checked_div"
                                                                       : "sun_llvm_checked_mod";
                const auto result = temporary();
                out_ << "  " << result << " = call i64 @" << helper << "(i64 "
                     << left.operand << ", i64 " << right.operand << ")\n";
                return {Type::integer, result};
            }
            unsupported("call to '" + expression.value + "'");
        }
        unsupported("expression node");
    }

    Value emit_binary(const Expression& expression) {
        if (expression.type == Type::boolean &&
            (expression.value == "&&" || expression.value == "||")) {
            const auto left = emit_expression(expression.children.at(0));
            const auto short_label = label("bool.short");
            const auto right_label = label("bool.right");
            const auto end_label = label("bool.end");
            const auto short_value = expression.value == "&&" ? "false" : "true";
            out_ << "  br i1 " << left.operand << ", label %"
                 << (expression.value == "&&" ? right_label : short_label)
                 << ", label %"
                 << (expression.value == "&&" ? short_label : right_label) << '\n'
                 << short_label << ":\n"
                 << "  br label %" << end_label << '\n'
                 << right_label << ":\n";
            const auto right = emit_expression(expression.children.at(1));
            out_ << "  br label %" << end_label << '\n'
                 << end_label << ":\n";
            const auto merged = temporary();
            out_ << "  " << merged << " = phi i1 [" << short_value
                 << ", %" << short_label << "], [" << right.operand
                 << ", %" << right_label << "]\n";
            return {Type::boolean, merged};
        }
        const auto left = emit_expression(expression.children.at(0));
        const auto right = emit_expression(expression.children.at(1));
        if (left.type.kind == Type::Kind::tuple &&
            (expression.value == "==" || expression.value == "!=")) {
            auto equal = emit_tuple_equal(left, right);
            if (expression.value == "!=") {
                const auto not_equal = temporary();
                out_ << "  " << not_equal << " = xor i1 " << equal << ", true\n";
                equal = not_equal;
            }
            return {Type::boolean, equal};
        }
        if (expression.type == Type::string && expression.value == "+") {
            const auto length_slot = temporary();
            out_ << "  " << length_slot << " = alloca i64\n";
            const auto result = temporary();
            out_ << "  " << result << " = call ptr @sun_llvm_string_concat(ptr "
                 << left.operand << ", i64 " << left.string_length << ", ptr "
                 << right.operand << ", i64 " << right.string_length << ", ptr "
                 << length_slot << ")\n";
            const auto length = temporary();
            out_ << "  " << length << " = load i64, ptr " << length_slot << '\n';
            return {Type::string, result, length};
        }
        const auto result = temporary();
        if (expression.type == Type::number) {
            const auto op = expression.value == "+" ? "fadd"
                          : expression.value == "-" ? "fsub"
                          : expression.value == "*" ? "fmul"
                          : expression.value == "/" ? "fdiv" : nullptr;
            if (!op) unsupported("number operator '" + expression.value + "'");
            out_ << "  " << result << " = " << op << " double "
                 << left.operand << ", " << right.operand << '\n';
            return {Type::number, result};
        }
        if (expression.type == Type::boolean) {
            if (expression.value == "&&" || expression.value == "||") {
                const auto short_label = label("bool.short");
                const auto right_label = label("bool.right");
                const auto end_label = label("bool.end");
                const auto short_value = expression.value == "&&" ? "false" : "true";
                out_ << "  br i1 " << left.operand << ", label %"
                     << (expression.value == "&&" ? right_label : short_label)
                     << ", label %"
                     << (expression.value == "&&" ? short_label : right_label) << '\n'
                     << short_label << ":\n"
                     << "  br label %" << end_label << '\n'
                     << right_label << ":\n";
                const auto right = emit_expression(expression.children.at(1));
                out_ << "  br label %" << end_label << '\n'
                     << end_label << ":\n";
                const auto merged = temporary();
                out_ << "  " << merged << " = phi i1 [" << short_value
                     << ", %" << short_label << "], [" << right.operand
                     << ", %" << right_label << "]\n";
                return {Type::boolean, merged};
            } else if (left.type == Type::number) {
                const auto predicate = expression.value == "==" ? "oeq"
                                     : expression.value == "!=" ? "une"
                                     : expression.value == "<" ? "olt"
                                     : expression.value == "<=" ? "ole"
                                     : expression.value == ">" ? "ogt" : "oge";
                out_ << "  " << result << " = fcmp " << predicate << " double "
                     << left.operand << ", " << right.operand << '\n';
            } else if (left.type == Type::integer || left.type == Type::boolean) {
                const auto predicate = expression.value == "==" ? "eq"
                                     : expression.value == "!=" ? "ne"
                                     : expression.value == "<" ? "slt"
                                     : expression.value == "<=" ? "sle"
                                     : expression.value == ">" ? "sgt" : "sge";
                out_ << "  " << result << " = icmp " << predicate << ' '
                     << llvm_type(left.type) << ' ' << left.operand << ", "
                     << right.operand << '\n';
            } else if (left.type == Type::character &&
                       (expression.value == "==" || expression.value == "!=")) {
                const auto same_length = temporary();
                const auto compare_length = temporary();
                const auto compared = temporary();
                const auto same_bytes = temporary();
                const auto equal = temporary();
                out_ << "  " << same_length << " = icmp eq i64 "
                     << left.string_length << ", " << right.string_length << '\n'
                     << "  " << compare_length << " = select i1 " << same_length
                     << ", i64 " << left.string_length << ", i64 0\n"
                     << "  " << compared << " = call i32 @memcmp(ptr "
                     << left.operand << ", ptr " << right.operand << ", i64 "
                     << compare_length << ")\n"
                     << "  " << same_bytes << " = icmp eq i32 " << compared
                     << ", 0\n"
                     << "  " << equal << " = and i1 " << same_length << ", "
                     << same_bytes << '\n';
                if (expression.value == "==") {
                    out_ << "  " << result << " = or i1 " << equal << ", false\n";
                } else {
                    out_ << "  " << result << " = xor i1 " << equal << ", true\n";
                }
            } else if (left.type == Type::string &&
                       (expression.value == "==" || expression.value == "!=")) {
                const auto same_length = temporary();
                const auto compare_length = temporary();
                const auto compared = temporary();
                const auto same_bytes = temporary();
                const auto equal = temporary();
                out_ << "  " << same_length << " = icmp eq i64 "
                     << left.string_length << ", " << right.string_length << '\n'
                     << "  " << compare_length << " = select i1 " << same_length
                     << ", i64 " << left.string_length << ", i64 0\n"
                     << "  " << compared << " = call i32 @memcmp(ptr "
                     << left.operand << ", ptr " << right.operand << ", i64 "
                     << compare_length << ")\n"
                     << "  " << same_bytes << " = icmp eq i32 " << compared
                     << ", 0\n"
                     << "  " << equal << " = and i1 " << same_length << ", "
                     << same_bytes << '\n';
                if (expression.value == "==") {
                    out_ << "  " << result << " = or i1 " << equal << ", false\n";
                } else {
                    out_ << "  " << result << " = xor i1 " << equal << ", true\n";
                }
            } else {
                unsupported("string comparison");
            }
            return {Type::boolean, result};
        }
        unsupported("binary expression result type '" + cpp_type(expression.type) + "'");
    }

    void emit_print(const PrintStatement& print) {
        const auto value = emit_expression(print.value);
        if (value.type.kind == Type::Kind::tuple) {
            emit_print_tuple(value);
        } else if (is_list_type(value.type)) {
            emit_print_list(value);
        } else if (is_dictionary_type(value.type) ||
                   value.type.kind == Type::Kind::empty_dictionary) {
            emit_print_dictionary(value, true);
           } else if (value.type == Type::bytes) {
              const auto length_slot = temporary();
              out_ << "  " << length_slot << " = alloca i64\n";
              const auto text = temporary();
              out_ << "  " << text << " = call ptr @sun_llvm_bytes_to_string(ptr "
                  << value.operand << ", ptr " << length_slot << ")\n";
              const auto length = temporary();
              out_ << "  " << length << " = load i64, ptr " << length_slot << '\n';
              const auto stream = temporary();
              const auto ignored = temporary();
              out_ << "  " << stream << " = load ptr, ptr @stdout\n"
                  << "  " << ignored << " = call i64 @fwrite(ptr " << text
                  << ", i64 " << length << ", i64 1, ptr " << stream << ")\n"
                  << "  call i32 @putchar(i32 10)\n";
        } else if (is_string_like(value.type)) {
            const auto stream = temporary();
            const auto ignored = temporary();
            out_ << "  " << stream << " = load ptr, ptr @stdout\n"
                 << "  " << ignored << " = call i64 @fwrite(ptr " << value.operand
                 << ", i64 " << value.string_length
                 << ", i64 1, ptr " << stream << ")\n"
                 << "  call i32 @putchar(i32 10)\n";
        } else if (value.type == Type::boolean) {
            const auto text = temporary();
            out_ << "  " << text << " = select i1 " << value.operand
                 << ", ptr @.sun.bool.true, ptr @.sun.bool.false\n"
                 << "  call i32 @puts(ptr " << text << ")\n";
        } else if (value.type == Type::integer) {
            out_ << "  call i32 (ptr, ...) @printf(ptr @.sun.fmt.int, i64 "
                 << value.operand << ")\n";
        } else if (value.type == Type::number) {
            out_ << "  call i32 (ptr, ...) @printf(ptr @.sun.fmt.number, double "
                 << value.operand << ")\n";
        } else if (value.type.kind == Type::Kind::custom &&
                   custom_types_.count(value.type.name) &&
                   custom_types_.at(value.type.name)->is_enum) {
            emit_print_enum(value);
        } else if (value.type.kind == Type::Kind::custom &&
                   custom_types_.count(value.type.name) &&
                   !custom_types_.at(value.type.name)->is_enum &&
                   !custom_types_.at(value.type.name)->is_error) {
            emit_print_custom(value);
        } else {
            unsupported("printing type '" + cpp_type(value.type) + "'");
        }
    }

    void emit_print_dictionary(const Value& value, bool newline) {
        const auto value_type = value.type.kind == Type::Kind::empty_dictionary
                                    ? Type::empty_list
                                    : dictionary_value_type(value.type);
        const bool pointer_backed = value_type.kind == Type::Kind::tuple ||
                                    (!is_string_like(value_type) &&
                                     llvm_type(value_type) == "ptr");
        if (value.type.kind == Type::Kind::empty_dictionary ||
            !pointer_backed) {
            const auto length_slot = temporary();
            out_ << "  " << length_slot << " = alloca i64\n";
            const auto text = temporary();
            out_ << "  " << text << " = call ptr @sun_llvm_dict_to_string(ptr "
                 << value.operand << ", ptr " << length_slot << ")\n";
            const auto length = temporary();
            out_ << "  " << length << " = load i64, ptr " << length_slot << '\n';
            const auto stream = temporary();
            const auto ignored = temporary();
            out_ << "  " << stream << " = load ptr, ptr @stdout\n"
                 << "  " << ignored << " = call i64 @fwrite(ptr " << text
                 << ", i64 " << length << ", i64 1, ptr " << stream << ")\n";
            if (newline) out_ << "  call i32 @putchar(i32 10)\n";
            return;
        }

        const auto length = temporary();
        out_ << "  " << length << " = call i64 @sun_llvm_dict_len(ptr "
             << value.operand << ")\n"
             << "  call i32 @putchar(i32 123)\n";
        const auto index_slot = temporary();
        out_ << "  " << index_slot << " = alloca i64\n"
             << "  store i64 0, ptr " << index_slot << '\n';
        const auto condition = label("dict.print.cond");
        const auto body = label("dict.print.body");
        const auto comma_label = label("dict.print.comma");
        const auto item_label = label("dict.print.item");
        const auto increment = label("dict.print.increment");
        const auto end = label("dict.print.end");

        out_ << "  br label %" << condition << '\n'
             << condition << ":\n";
        const auto index = temporary();
        out_ << "  " << index << " = load i64, ptr " << index_slot << '\n';
        const auto more = temporary();
        out_ << "  " << more << " = icmp slt i64 " << index << ", " << length << '\n'
             << "  br i1 " << more << ", label %" << body
             << ", label %" << end << '\n'
             << body << ":\n";
        const auto not_first = temporary();
        out_ << "  " << not_first << " = icmp sgt i64 " << index << ", 0\n"
             << "  br i1 " << not_first << ", label %" << comma_label
             << ", label %" << item_label << '\n'
             << comma_label << ":\n"
             << "  call i32 @printf(ptr @.sun.fmt.comma)\n"
             << "  br label %" << item_label << '\n'
             << item_label << ":\n";
        const auto index_number = temporary();
        const auto key_length_slot = temporary();
        const auto key = temporary();
        const auto key_length = temporary();
        const auto stream = temporary();
        const auto ignored = temporary();
        out_ << "  " << index_number << " = sitofp i64 " << index << " to double\n"
             << "  " << key_length_slot << " = alloca i64\n"
             << "  " << key << " = call ptr @sun_llvm_dict_key_at(ptr "
             << value.operand << ", double " << index_number << ", ptr "
             << key_length_slot << ")\n"
             << "  " << key_length << " = load i64, ptr " << key_length_slot << '\n'
             << "  " << stream << " = load ptr, ptr @stdout\n"
             << "  " << ignored << " = call i64 @fwrite(ptr " << key
             << ", i64 " << key_length << ", i64 1, ptr " << stream << ")\n"
             << "  call i32 (ptr, ...) @printf(ptr "
             << string_pointer(": ") << ")\n";
        const auto item_pointer = temporary();
        out_ << "  " << item_pointer << " = call ptr @sun_llvm_dict_value_ptr_at(ptr "
             << value.operand << ", double " << index_number << ")\n";
        emit_print_inline(unbox_collection_item(value_type, item_pointer));
        out_ << "  br label %" << increment << '\n'
             << increment << ":\n";
        const auto next_index = temporary();
        out_ << "  " << next_index << " = add i64 " << index << ", 1\n"
             << "  store i64 " << next_index << ", ptr " << index_slot << '\n'
             << "  br label %" << condition << '\n'
             << end << ":\n"
             << "  call i32 @putchar(i32 125)\n";
        if (newline) out_ << "  call i32 @putchar(i32 10)\n";
    }

    void emit_print_inline(const Value& value) {
        if (value.type.kind == Type::Kind::tuple) {
            emit_print_tuple(value, false);
        } else if (is_list_type(value.type)) {
            emit_print_list(value, false);
        } else if (is_dictionary_type(value.type) ||
                   value.type.kind == Type::Kind::empty_dictionary) {
            emit_print_dictionary(value, false);
        } else if (value.type.kind == Type::Kind::custom &&
                   custom_types_.count(value.type.name) &&
                   custom_types_.at(value.type.name)->is_enum) {
            emit_print_enum(value, false);
        } else if (value.type.kind == Type::Kind::custom &&
                   custom_types_.count(value.type.name) &&
                   !custom_types_.at(value.type.name)->is_error) {
            emit_print_custom(value, false);
        } else if (is_string_like(value.type)) {
            const auto stream = temporary();
            const auto ignored = temporary();
            out_ << "  " << stream << " = load ptr, ptr @stdout\n"
                 << "  " << ignored << " = call i64 @fwrite(ptr " << value.operand
                 << ", i64 " << value.string_length << ", i64 1, ptr " << stream
                 << ")\n";
        } else if (value.type == Type::boolean) {
            const auto text = temporary();
            out_ << "  " << text << " = select i1 " << value.operand
                 << ", ptr @.sun.bool.true, ptr @.sun.bool.false\n"
                 << "  call i32 (ptr, ...) @printf(ptr @.sun.fmt.string, ptr "
                 << text << ")\n";
        } else if (value.type == Type::integer) {
            out_ << "  call i32 (ptr, ...) @printf(ptr @.sun.fmt.int.no_newline, i64 "
                 << value.operand << ")\n";
        } else if (value.type == Type::number) {
            out_ << "  call i32 (ptr, ...) @printf(ptr @.sun.fmt.number.no_newline, double "
                 << value.operand << ")\n";
        } else {
            unsupported("printing nested value type '" + cpp_type(value.type) + "'");
        }
    }

    void emit_print_list(const Value& value, bool newline = true) {
        const auto item_type = list_item_type(value.type);
        const auto suffix = llvm_list_suffix(item_type);
        const auto length = temporary();
        out_ << "  " << length << " = call i64 @sun_llvm_list_len_" << suffix
             << "(ptr " << value.operand << ")\n"
             << "  call i32 @putchar(i32 91)\n";

        const auto index_slot = temporary();
        out_ << "  " << index_slot << " = alloca i64\n"
             << "  store i64 0, ptr " << index_slot << '\n';
        const auto condition = label("list.print.cond");
        const auto body = label("list.print.body");
        const auto comma_label = label("list.print.comma");
        const auto item_label = label("list.print.item");
        const auto increment = label("list.print.increment");
        const auto end = label("list.print.end");

        out_ << "  br label %" << condition << '\n'
             << condition << ":\n";
        const auto index = temporary();
        out_ << "  " << index << " = load i64, ptr " << index_slot << '\n';
        const auto more = temporary();
        out_ << "  " << more << " = icmp slt i64 " << index << ", " << length << '\n'
             << "  br i1 " << more << ", label %" << body
             << ", label %" << end << '\n'
             << body << ":\n";
        const auto not_first = temporary();
        out_ << "  " << not_first << " = icmp sgt i64 " << index << ", 0\n"
             << "  br i1 " << not_first << ", label %" << comma_label
             << ", label %" << item_label << '\n'
             << comma_label << ":\n"
             << "  call i32 @printf(ptr @.sun.fmt.comma)\n"
             << "  br label %" << item_label << '\n'
             << item_label << ":\n";

        const auto index_number = temporary();
        out_ << "  " << index_number << " = sitofp i64 " << index << " to double\n";
        if (is_string_like(item_type)) {
            const auto item_length_slot = temporary();
            out_ << "  " << item_length_slot << " = alloca i64\n";
            const auto item = temporary();
            out_ << "  " << item << " = call ptr @sun_llvm_list_at_string(ptr "
                 << value.operand << ", double " << index_number << ", ptr "
                 << item_length_slot << ")\n";
            const auto item_length = temporary();
            const auto stream = temporary();
            const auto ignored = temporary();
            out_ << "  " << item_length << " = load i64, ptr " << item_length_slot << '\n'
                 << "  " << stream << " = load ptr, ptr @stdout\n"
                 << "  " << ignored << " = call i64 @fwrite(ptr " << item
                 << ", i64 " << item_length << ", i64 1, ptr " << stream << ")\n";
        } else if (item_type == Type::boolean) {
            const auto item = temporary();
            const auto text = temporary();
            out_ << "  " << item << " = call i1 @sun_llvm_list_at_bool(ptr "
                 << value.operand << ", double " << index_number << ")\n"
                 << "  " << text << " = select i1 " << item
                 << ", ptr @.sun.bool.true, ptr @.sun.bool.false\n"
                 << "  call i32 (ptr, ...) @printf(ptr @.sun.fmt.string, ptr "
                 << text << ")\n";
        } else if (item_type == Type::integer) {
            const auto item = temporary();
            out_ << "  " << item << " = call i64 @sun_llvm_list_at_i64(ptr "
                 << value.operand << ", double " << index_number << ")\n"
                 << "  call i32 (ptr, ...) @printf(ptr @.sun.fmt.int.no_newline, i64 "
                 << item << ")\n";
        } else if (item_type == Type::number) {
            const auto item = temporary();
            out_ << "  " << item << " = call double @sun_llvm_list_at_double(ptr "
                 << value.operand << ", double " << index_number << ")\n"
                 << "  call i32 (ptr, ...) @printf(ptr @.sun.fmt.number.no_newline, double "
                 << item << ")\n";
        } else if (item_type.kind == Type::Kind::tuple ||
                   llvm_type(item_type) == "ptr") {
            const auto item_pointer = temporary();
            out_ << "  " << item_pointer << " = call ptr @sun_llvm_list_at_ptr(ptr "
                 << value.operand << ", double " << index_number << ")\n";
            emit_print_inline(unbox_collection_item(item_type, item_pointer));
        }

        out_ << "  br label %" << increment << '\n'
             << increment << ":\n";
        const auto next_index = temporary();
        out_ << "  " << next_index << " = add i64 " << index << ", 1\n"
             << "  store i64 " << next_index << ", ptr " << index_slot << '\n'
             << "  br label %" << condition << '\n'
             << end << ":\n"
             << "  call i32 @putchar(i32 93)\n";
        if (newline) out_ << "  call i32 @putchar(i32 10)\n";
    }

    void emit_statement(const Statement& statement) {
        if (const auto* print = std::get_if<PrintStatement>(&statement.node)) {
            emit_print(*print);
        } else if (const auto* print_color =
                       std::get_if<PrintColorStatement>(&statement.node)) {
            const auto value = emit_expression(print_color->value);
            const auto prefix = string_pointer(print_color->color_prefix);
            const auto reset = string_pointer("\x1b[0m");
            const auto stream = temporary();
            const auto ignored_prefix = temporary();
            const auto ignored_value = temporary();
            const auto ignored_reset = temporary();
            out_ << "  " << stream << " = load ptr, ptr @stdout\n"
                 << "  " << ignored_prefix << " = call i64 @fwrite(ptr " << prefix
                 << ", i64 " << print_color->color_prefix.size()
                 << ", i64 1, ptr " << stream << ")\n"
                 << "  " << ignored_value << " = call i64 @fwrite(ptr " << value.operand
                 << ", i64 " << value.string_length << ", i64 1, ptr " << stream << ")\n"
                 << "  " << ignored_reset << " = call i64 @fwrite(ptr " << reset
                 << ", i64 4, i64 1, ptr " << stream << ")\n"
                 << "  call i32 @putchar(i32 10)\n";
        } else if (const auto* declaration =
                       std::get_if<VariableDeclarationStatement>(&statement.node)) {
            const auto slot = "%sun.slot." + declaration->cpp_name;
            Value value;
            if (declaration->initializer.kind == Expression::Kind::empty_list) {
                const auto suffix = llvm_list_suffix(list_item_type(declaration->type));
                const auto result = temporary();
                out_ << "  " << result << " = call ptr @sun_llvm_list_create_"
                     << suffix << "()\n";
                value = {declaration->type, result};
            } else if (declaration->initializer.kind ==
                       Expression::Kind::empty_dictionary) {
                const auto result = temporary();
                out_ << "  " << result << " = call ptr @sun_llvm_dict_create()\n";
                value = {declaration->type, result};
            } else {
                value = emit_expression(declaration->initializer);
            }
            if (declaration->type.kind == Type::Kind::custom &&
                (declaration->initializer.kind == Expression::Kind::variable ||
                 declaration->initializer.kind == Expression::Kind::member)) {
                value.operand = clone_custom_value(value);
            }
            out_ << "  store " << llvm_type(declaration->type) << ' '
                 << value.operand << ", ptr " << slot << '\n';
            if (is_string_like(declaration->type)) {
                out_ << "  store i64 " << value.string_length << ", ptr %sun.len."
                     << declaration->cpp_name << '\n';
            }
        } else if (const auto* assignment =
                       std::get_if<AssignmentStatement>(&statement.node)) {
            const auto found = variables_.find(assignment->cpp_name);
            if (found == variables_.end()) unsupported("assignment target");
            auto value = emit_expression(assignment->value);
            if (found->second.kind == Type::Kind::custom) {
                value.operand = clone_custom_value(value);
            }
            out_ << "  store " << llvm_type(found->second) << ' ' << value.operand
                 << ", " << slot_pointer(assignment->cpp_name) << '\n';
            if (is_string_like(found->second)) {
                out_ << "  store i64 " << value.string_length << ", "
                     << length_pointer(assignment->cpp_name) << '\n';
            }
        } else if (const auto* member_assignment =
                       std::get_if<MemberAssignmentStatement>(&statement.node)) {
            if (member_assignment->target.kind != Expression::Kind::member ||
                member_assignment->target.children.size() != 1) {
                unsupported("member assignment target");
            }
            const auto object = emit_expression(
                member_assignment->target.children.front());
            const auto pointer = emit_field_pointer(
                object, member_assignment->target.value);
            const auto value = emit_expression(member_assignment->value);
            if (is_string_like(member_assignment->target.type)) {
                const auto data_pointer = temporary();
                const auto length_pointer = temporary();
                out_ << "  " << data_pointer
                     << " = getelementptr { ptr, i64 }, ptr " << pointer
                     << ", i32 0, i32 0\n"
                     << "  " << length_pointer
                     << " = getelementptr { ptr, i64 }, ptr " << pointer
                     << ", i32 0, i32 1\n"
                     << "  store ptr " << value.operand << ", ptr " << data_pointer
                     << '\n'
                     << "  store i64 " << value.string_length << ", ptr "
                     << length_pointer << '\n';
            } else {
                out_ << "  store " << llvm_type(member_assignment->target.type)
                     << ' ' << value.operand << ", ptr " << pointer << '\n';
            }
        } else if (const auto* returned = std::get_if<ReturnStatement>(&statement.node)) {
            std::optional<Value> value;
            if (returned->value) {
                value = emit_expression(*returned->value);
            }
            emit_finally_stack();
            if (value) {
                if (value->type.kind == Type::Kind::custom) {
                    value->operand = clone_custom_value(*value);
                }
                if (is_string_like(value->type)) {
                    out_ << "  store i64 " << value->string_length
                         << ", ptr %sun.return.len\n";
                }
                out_ << "  ret " << llvm_type(value->type) << ' ' << value->operand << '\n';
            } else {
                out_ << "  ret void\n";
            }
            terminated_ = true;
            } else if (const auto* call = std::get_if<CallStatement>(&statement.node)) {
                if (call->callee.rfind("SunType_", 0) == 0 &&
                    call->callee.size() > 11 &&
                    call->callee.substr(call->callee.size() - 6) == "::init") {
                    const auto owner_end = call->callee.find("::");
                    if (owner_end == std::string::npos) {
                        unsupported("base initializer call");
                    }
                    const auto owner = call->callee.substr(8, owner_end - 8);
                    std::ostringstream arguments;
                    arguments << "ptr %sun.this";
                    if (!call->arguments.empty()) arguments << ", ";
                    const auto* initializer = find_method(owner, "init");
                    if (!initializer || initializer->owner_type != owner) {
                        unsupported("base initializer call");
                    }
                    emit_call_arguments(arguments, call->arguments, *initializer);
                    out_ << "  call void @sun_method_" << owner << "_init("
                         << arguments.str() << ")\n";
                } else if (call->callee == "sun_io_write" &&
                           call->arguments.size() == 2) {
                    const auto handle = emit_expression(call->arguments[0]);
                    const auto contents = emit_expression(call->arguments[1]);
                    out_ << "  call void @sun_llvm_io_write(i64 " << handle.operand
                         << ", ptr " << contents.operand << ", i64 "
                         << contents.string_length << ")\n";
                } else if ((call->callee == "sun_io_close" ||
                            call->callee == "sun_io_close_noexcept") &&
                           call->arguments.size() == 1) {
                    const auto handle = emit_expression(call->arguments[0]);
                    out_ << "  call void @sun_llvm_io_"
                         << (call->callee == "sun_io_close" ? "close" : "close_noexcept")
                         << "(i64 " << handle.operand << ")\n";
                } else if (call->callee == "sun_bytes_append") {
                    const auto bytes = emit_expression(call->arguments.at(0));
                    const auto value = emit_expression(call->arguments.at(1));
                    const auto integer = temporary();
                    out_ << "  " << integer
                         << " = call i64 @sun_llvm_checked_number_to_int(double "
                         << value.operand << ")\n"
                         << "  call void @sun_llvm_bytes_append(ptr " << bytes.operand
                         << ", i64 " << integer << ")\n";
                } else if (call->callee == "sun_list_append") {
                    const auto list = emit_expression(call->arguments.at(0));
                    const auto item = emit_expression(call->arguments.at(1));
                    const auto suffix = llvm_list_suffix(item.type);
                    if (is_string_like(item.type)) {
                        out_ << "  call void @sun_llvm_list_append_string(ptr "
                             << list.operand << ", ptr " << item.operand << ", i64 "
                             << item.string_length << ")\n";
                    } else if (item.type.kind == Type::Kind::tuple) {
                        const auto boxed = box_collection_item(item);
                        out_ << "  call void @sun_llvm_list_append_ptr(ptr "
                             << list.operand << ", ptr "
                             << boxed << ")\n";
                    } else {
                        out_ << "  call void @sun_llvm_list_append_" << suffix
                             << "(ptr " << list.operand << ", " << llvm_type(item.type)
                             << " " << item.operand << ")\n";
                    }
                } else if (call->callee == "sun_list_insert") {
                    const auto list = emit_expression(call->arguments.at(0));
                    auto index = emit_expression(call->arguments.at(1));
                    const auto item = emit_expression(call->arguments.at(2));
                    if (index.type == Type::integer) {
                        const auto converted = temporary();
                        out_ << "  " << converted << " = sitofp i64 " << index.operand
                             << " to double\n";
                        index = {Type::number, converted};
                    }
                    const auto suffix = llvm_list_suffix(item.type);
                    if (is_string_like(item.type)) {
                        out_ << "  call void @sun_llvm_list_insert_string(ptr "
                             << list.operand << ", double " << index.operand
                             << ", ptr " << item.operand << ", i64 "
                             << item.string_length << ")\n";
                    } else if (item.type.kind == Type::Kind::tuple) {
                        const auto boxed = box_collection_item(item);
                        out_ << "  call void @sun_llvm_list_insert_ptr(ptr "
                             << list.operand << ", double " << index.operand
                             << ", ptr " << boxed << ")\n";
                    } else {
                        out_ << "  call void @sun_llvm_list_insert_" << suffix
                             << "(ptr " << list.operand << ", double " << index.operand
                             << ", " << llvm_type(item.type) << " " << item.operand
                             << ")\n";
                    }
                } else if (call->callee == "sun_list_remove") {
                    const auto list = emit_expression(call->arguments.at(0));
                    const auto item = emit_expression(call->arguments.at(1));
                    const auto suffix = llvm_list_suffix(item.type);
                    if (is_string_like(item.type)) {
                        out_ << "  call i1 @sun_llvm_list_remove_string(ptr "
                             << list.operand << ", ptr " << item.operand << ", i64 "
                             << item.string_length << ")\n";
                    } else if (item.type.kind == Type::Kind::tuple) {
                        const auto boxed = box_collection_item(item);
                        out_ << "  call i1 @sun_llvm_list_remove_ptr(ptr "
                             << list.operand << ", ptr "
                             << boxed << ")\n";
                    } else {
                        out_ << "  call i1 @sun_llvm_list_remove_" << suffix
                             << "(ptr " << list.operand << ", " << llvm_type(item.type)
                             << " " << item.operand << ")\n";
                    }
                } else if (call->callee == "sun_list_pop") {
                    const auto list = emit_expression(call->arguments.at(0));
                    auto index = emit_expression(call->arguments.at(1));
                    if (index.type == Type::integer) {
                        const auto converted = temporary();
                        out_ << "  " << converted << " = sitofp i64 " << index.operand
                             << " to double\n";
                        index = {Type::number, converted};
                    }
                    const auto item_type = list_item_type(call->arguments.at(0).type);
                    const auto suffix = llvm_list_suffix(item_type);
                    if (is_string_like(item_type)) {
                        const auto length_slot = temporary();
                        out_ << "  " << length_slot << " = alloca i64\n"
                             << "  call ptr @sun_llvm_list_pop_string(ptr "
                             << list.operand << ", double " << index.operand
                             << ", ptr " << length_slot << ")\n";
                    } else if (item_type.kind == Type::Kind::tuple) {
                        out_ << "  call ptr @sun_llvm_list_pop_ptr(ptr "
                             << list.operand << ", double " << index.operand << ")\n";
                    } else {
                        out_ << "  call " << llvm_type(item_type)
                             << " @sun_llvm_list_pop_" << suffix << "(ptr "
                             << list.operand << ", double " << index.operand << ")\n";
                    }
                } else {
                const auto function = functions_.find(call->callee);
                if (function == functions_.end()) unsupported("call to '" + call->callee + "'");
                std::ostringstream arguments;
                emit_call_arguments(arguments, call->arguments, *function->second);
                emit_captured_arguments(arguments, call->callee);
                auto argument_text = arguments.str();
                if (function->second->return_type &&
                    is_string_like(*function->second->return_type)) {
                    const auto length_slot = temporary();
                    out_ << "  " << length_slot << " = alloca i64\n";
                    if (!argument_text.empty()) argument_text += ", ";
                    argument_text += "ptr " + length_slot;
                }
                out_ << "  call "
                 << (function->second->return_type
                     ? llvm_type(*function->second->return_type)
                     : "void")
                 << " @" << call->callee << '(' << argument_text << ")\n";
                }
        } else if (const auto* indexed =
                       std::get_if<IndexedAssignmentStatement>(&statement.node)) {
            if (indexed->kind == IndexedAssignmentStatement::Kind::list &&
                indexed->object.kind == Expression::Kind::call &&
                indexed->object.value == "sun_list_at" &&
                indexed->object.children.size() == 2) {
                const auto list = emit_expression(indexed->object.children[0]);
                auto index = emit_expression(indexed->object.children[1]);
                const auto item = emit_expression(indexed->value);
                if (index.type == Type::integer) {
                    const auto converted = temporary();
                    out_ << "  " << converted << " = sitofp i64 " << index.operand
                         << " to double\n";
                    index = {Type::number, converted};
                }
                const auto suffix = llvm_list_suffix(item.type);
                if (is_string_like(item.type)) {
                    out_ << "  call void @sun_llvm_list_set_string(ptr "
                         << list.operand << ", double " << index.operand
                         << ", ptr " << item.operand << ", i64 "
                         << item.string_length << ")\n";
                } else if (item.type.kind == Type::Kind::tuple) {
                    const auto boxed = box_collection_item(item);
                    out_ << "  call void @sun_llvm_list_set_ptr(ptr "
                         << list.operand << ", double " << index.operand
                         << ", ptr " << boxed << ")\n";
                } else {
                    out_ << "  call void @sun_llvm_list_set_" << suffix
                         << "(ptr " << list.operand << ", double " << index.operand
                         << ", " << llvm_type(item.type) << " " << item.operand
                         << ")\n";
                }
            } else if (indexed->kind == IndexedAssignmentStatement::Kind::dictionary) {
                const auto dict = emit_expression(indexed->object);
                const auto key = emit_expression(indexed->key);
                const auto value = emit_expression(indexed->value);
                const auto pointer_value = box_collection_item(value);
                const bool key_string = key.type == Type::string || key.type == Type::character;
                if (key_string) {
                    if (is_string_like(value.type)) out_ << "  call void @sun_llvm_dict_set_string_string(ptr " << dict.operand << ", ptr " << key.operand << ", i64 " << key.string_length << ", ptr " << value.operand << ", i64 " << value.string_length << ")\n";
                    else if (value.type == Type::integer) out_ << "  call void @sun_llvm_dict_set_string_i64(ptr " << dict.operand << ", ptr " << key.operand << ", i64 " << key.string_length << ", i64 " << value.operand << ")\n";
                    else if (value.type == Type::number) out_ << "  call void @sun_llvm_dict_set_string_double(ptr " << dict.operand << ", ptr " << key.operand << ", i64 " << key.string_length << ", double " << value.operand << ")\n";
                    else if (value.type == Type::boolean) out_ << "  call void @sun_llvm_dict_set_string_bool(ptr " << dict.operand << ", ptr " << key.operand << ", i64 " << key.string_length << ", i1 " << value.operand << ")\n";
                    else if (llvm_type(value.type) == "ptr" ||
                             value.type.kind == Type::Kind::tuple) out_ << "  call void @sun_llvm_dict_set_string_ptr(ptr " << dict.operand << ", ptr " << key.operand << ", i64 " << key.string_length << ", ptr " << pointer_value << ")\n";
                } else if (key.type == Type::integer) {
                    if (is_string_like(value.type)) out_ << "  call void @sun_llvm_dict_set_i64_string(ptr " << dict.operand << ", i64 " << key.operand << ", ptr " << value.operand << ", i64 " << value.string_length << ")\n";
                    else if (value.type == Type::integer) out_ << "  call void @sun_llvm_dict_set_i64_i64(ptr " << dict.operand << ", i64 " << key.operand << ", i64 " << value.operand << ")\n";
                    else if (value.type == Type::number) out_ << "  call void @sun_llvm_dict_set_i64_double(ptr " << dict.operand << ", i64 " << key.operand << ", double " << value.operand << ")\n";
                    else if (value.type == Type::boolean) out_ << "  call void @sun_llvm_dict_set_i64_bool(ptr " << dict.operand << ", i64 " << key.operand << ", i1 " << value.operand << ")\n";
                    else if (llvm_type(value.type) == "ptr" ||
                             value.type.kind == Type::Kind::tuple) out_ << "  call void @sun_llvm_dict_set_i64_ptr(ptr " << dict.operand << ", i64 " << key.operand << ", ptr " << pointer_value << ")\n";
                } else if (key.type == Type::boolean) {
                    if (is_string_like(value.type)) out_ << "  call void @sun_llvm_dict_set_bool_string(ptr " << dict.operand << ", i1 " << key.operand << ", ptr " << value.operand << ", i64 " << value.string_length << ")\n";
                    else if (value.type == Type::integer) out_ << "  call void @sun_llvm_dict_set_bool_i64(ptr " << dict.operand << ", i1 " << key.operand << ", i64 " << value.operand << ")\n";
                    else if (value.type == Type::number) out_ << "  call void @sun_llvm_dict_set_bool_double(ptr " << dict.operand << ", i1 " << key.operand << ", double " << value.operand << ")\n";
                    else if (value.type == Type::boolean) out_ << "  call void @sun_llvm_dict_set_bool_bool(ptr " << dict.operand << ", i1 " << key.operand << ", i1 " << value.operand << ")\n";
                    else if (llvm_type(value.type) == "ptr" ||
                             value.type.kind == Type::Kind::tuple) out_ << "  call void @sun_llvm_dict_set_bool_ptr(ptr " << dict.operand << ", i1 " << key.operand << ", ptr " << pointer_value << ")\n";
                } else unsupported("dictionary key type");
            } else if (indexed->kind == IndexedAssignmentStatement::Kind::bytes) {
                const auto bytes = emit_expression(indexed->object);
                auto index = emit_expression(indexed->key);
                const auto value = emit_expression(indexed->value);
                if (index.type == Type::integer) {
                    const auto converted = temporary();
                    out_ << "  " << converted << " = sitofp i64 " << index.operand
                         << " to double\n";
                    index = {Type::number, converted};
                }
                 const auto integer_value = temporary();
                 out_ << "  " << integer_value
                     << " = call i64 @sun_llvm_checked_number_to_int(double "
                     << value.operand << ")\n";
                out_ << "  call void @sun_llvm_bytes_set(ptr " << bytes.operand
                     << ", double " << index.operand << ", i64 " << integer_value
                     << ")\n";
            } else {
                unsupported("indexed assignment for non-list collection");
            }
        } else if (const auto* member_call =
                       std::get_if<MemberCallStatement>(&statement.node)) {
            if (member_call->method == "clear" &&
                is_dictionary_type(member_call->receiver.type)) {
                const auto dict = emit_expression(member_call->receiver);
                out_ << "  call void @sun_llvm_dict_clear(ptr " << dict.operand << ")\n";
            } else if (member_call->method == "clear" &&
                member_call->receiver.type == Type::bytes) {
                const auto bytes = emit_expression(member_call->receiver);
                out_ << "  call void @sun_llvm_bytes_clear(ptr " << bytes.operand << ")\n";
            } else if (member_call->method == "append" &&
                       member_call->receiver.type == Type::bytes &&
                       member_call->arguments.size() == 1) {
                const auto bytes = emit_expression(member_call->receiver);
                const auto value = emit_expression(member_call->arguments[0]);
                out_ << "  call void @sun_llvm_bytes_append(ptr " << bytes.operand
                     << ", i64 " << value.operand << ")\n";
            } else if (member_call->method == "clear" &&
                is_list_type(member_call->receiver.type)) {
                const auto list = emit_expression(member_call->receiver);
                const auto suffix = llvm_list_suffix(
                    list_item_type(member_call->receiver.type));
                out_ << "  call void @sun_llvm_list_clear_" << suffix
                     << "(ptr " << list.operand << ")\n";
            } else if (member_call->receiver.type.kind == Type::Kind::custom &&
                       custom_types_.count(member_call->receiver.type.name) &&
                       !custom_types_.at(member_call->receiver.type.name)->is_enum) {
                const auto* method = find_method(member_call->receiver.type.name,
                                                 member_call->method);
                if (!method || method->return_type) {
                    unsupported("void method '" + member_call->method + "'");
                }
                const auto receiver = emit_expression(member_call->receiver);
                std::ostringstream arguments;
                arguments << "ptr " << receiver.operand;
                if (!member_call->arguments.empty()) arguments << ", ";
                emit_call_arguments(arguments, member_call->arguments, *method);
                out_ << "  call void @sun_method_" << method->owner_type << '_'
                     << method->name << '(' << arguments.str() << ")\n";
            } else {
                unsupported("member call '" + member_call->method + "'");
            }
        } else if (const auto* assertion =
                       std::get_if<AssertStatement>(&statement.node)) {
            const auto condition = emit_expression(assertion->condition);
            out_ << "  call void @sun_llvm_assert(i1 " << condition.operand << ")\n";
        } else if (const auto* raised = std::get_if<RaiseStatement>(&statement.node)) {
            const auto error = emit_expression(raised->error);
            out_ << "  call void @sun_llvm_raise_custom(ptr " << error.operand << ")\n"
                 << "  unreachable\n";
            terminated_ = true;
        } else if (const auto* control =
                       std::get_if<LoopControlStatement>(&statement.node)) {
            if (loop_targets_.empty()) unsupported("loop control outside a loop");
            out_ << "  br label %"
                 << (control->kind == LoopControlStatement::Kind::break_
                         ? loop_targets_.back().first
                         : loop_targets_.back().second)
                 << '\n';
            terminated_ = true;
        } else if (const auto* conditional = std::get_if<IfStatement>(&statement.node)) {
            emit_if(*conditional);
        } else if (const auto* matched = std::get_if<MatchStatement>(&statement.node)) {
            emit_match(*matched);
        } else if (const auto* loop = std::get_if<WhileStatement>(&statement.node)) {
            emit_while(*loop);
        } else if (const auto* loop =
                       std::get_if<RangeForStatement>(&statement.node)) {
            emit_range_for(*loop);
        } else if (const auto* loop =
                       std::get_if<ForEachStatement>(&statement.node)) {
            emit_for_each(*loop);
        } else if (const auto* attempt =
                       std::get_if<TryCatchStatement>(&statement.node)) {
            emit_try(*attempt);
        } else if (const auto* with_open =
                       std::get_if<WithOpenStatement>(&statement.node)) {
            const auto path = emit_expression(with_open->path);
            const auto mode = emit_expression(with_open->mode);
            const auto handle = temporary();
            out_ << "  " << handle << " = call i64 @sun_llvm_io_open(ptr "
                 << path.operand << ", i64 " << path.string_length << ", ptr "
                 << mode.operand << ", i64 " << mode.string_length << ")\n"
                 << "  store i64 " << handle << ", ptr %sun.slot."
                 << with_open->handle_name << '\n';
            Expression handle_expression;
            handle_expression.kind = Expression::Kind::variable;
            handle_expression.type = Type::integer;
            handle_expression.value = with_open->handle_name;
            Block cleanup{Statement{
                CallStatement{"sun_io_close_noexcept", {handle_expression}},
                statement.line}};
            TryCatchStatement guarded;
            guarded.try_body = with_open->body;
            guarded.finally_body = std::move(cleanup);
            emit_try(guarded);
        } else if (std::get_if<LocalFunctionStatement>(&statement.node)) {
            return;
        } else {
            unsupported("statement");
        }
    }

    void emit_block(const Block& block) {
        for (const auto& statement : block) {
            if (terminated_) break;
            emit_statement(statement);
        }
    }

    void predeclare_block(const Block& block) {
        for (const auto& statement : block) {
            if (const auto* declaration =
                    std::get_if<VariableDeclarationStatement>(&statement.node)) {
                variables_.emplace(declaration->cpp_name, declaration->type);
            } else if (const auto* matched =
                           std::get_if<MatchStatement>(&statement.node)) {
                for (const auto& match_case : matched->cases) {
                    if (!match_case.enum_variant_type.empty()) {
                        const auto prefix = "SunType_" + matched->value.type.name + "_";
                        if (match_case.enum_variant_type.rfind(prefix, 0) != 0) {
                            unsupported("enum match binding type");
                        }
                        const auto variant_name =
                            match_case.enum_variant_type.substr(prefix.size());
                        const auto& variants =
                            custom_types_.at(matched->value.type.name)->enum_variants;
                        const auto variant = std::find_if(
                            variants.begin(), variants.end(),
                            [&](const EnumVariant& candidate) {
                                return candidate.name == variant_name;
                            });
                        if (variant == variants.end()) {
                            unsupported("unknown enum match variant");
                        }
                        for (const auto& binding : match_case.bindings) {
                            const auto field = std::find_if(
                                variant->fields.begin(), variant->fields.end(),
                                [&](const Field& candidate) {
                                    return candidate.name == binding.field_name;
                                });
                            if (field == variant->fields.end()) {
                                unsupported("unknown enum match field");
                            }
                            variables_.emplace(binding.cpp_name, field->type);
                        }
                    }
                    predeclare_block(match_case.body);
                }
                if (matched->else_body) predeclare_block(*matched->else_body);
            } else if (const auto* conditional =
                           std::get_if<IfStatement>(&statement.node)) {
                for (const auto& branch : conditional->branches) {
                    predeclare_block(branch.body);
                }
                if (conditional->else_body) predeclare_block(*conditional->else_body);
            } else if (const auto* loop =
                           std::get_if<WhileStatement>(&statement.node)) {
                predeclare_block(loop->body);
            } else if (const auto* loop =
                           std::get_if<RangeForStatement>(&statement.node)) {
                variables_.emplace(loop->variable_name, Type::number);
                predeclare_block(loop->body);
            } else if (const auto* loop =
                           std::get_if<ForEachStatement>(&statement.node)) {
                if (loop->dictionary && is_dictionary_type(loop->collection.type)) {
                    variables_.emplace(loop->variable_name,
                                       dictionary_key_type(loop->collection.type));
                } else if (is_list_type(loop->collection.type)) {
                    variables_.emplace(loop->variable_name,
                                       list_item_type(loop->collection.type));
                } else if (loop->collection.type == Type::bytes) {
                    variables_.emplace(loop->variable_name, Type::integer);
                } else {
                    unsupported("for-in collection type '" + cpp_type(loop->collection.type) + "'");
                }
                predeclare_block(loop->body);
            } else if (const auto* with_open =
                           std::get_if<WithOpenStatement>(&statement.node)) {
                variables_.emplace(with_open->handle_name, Type::integer);
                predeclare_block(with_open->body);
            } else if (const auto* attempt =
                           std::get_if<TryCatchStatement>(&statement.node)) {
                predeclare_block(attempt->try_body);
                for (const auto& handler : attempt->handlers) {
                    if (handler.message_name) {
                        variables_.emplace(*handler.message_name, Type::string);
                    }
                    predeclare_block(handler.body);
                }
                if (attempt->finally_body) {
                    predeclare_block(*attempt->finally_body);
                }
            }
        }
    }

    void emit_match(const MatchStatement& statement) {
        const auto value = emit_expression(statement.value);
        const bool enum_value = value.type.kind == Type::Kind::custom &&
            custom_types_.count(value.type.name) &&
            custom_types_.at(value.type.name)->is_enum;
        const auto enum_tag = enum_value ? emit_enum_tag(value) : std::string();
        const auto end_label = label("match.end");
        for (const auto& match_case : statement.cases) {
            const auto matched = temporary();
            const auto& variant_type = match_case.enum_variant_type;
            if (enum_value && !variant_type.empty()) {
                const auto prefix = "SunType_" + value.type.name + "_";
                if (variant_type.rfind(prefix, 0) != 0) {
                    unsupported("enum match variant type");
                }
                const auto variant_name = variant_type.substr(prefix.size());
                out_ << "  " << matched << " = icmp eq i32 " << enum_tag
                     << ", " << enum_variant_index(value.type.name, variant_name)
                     << '\n';
            } else if (enum_value) {
                const auto pattern = emit_expression(match_case.pattern);
                const auto pattern_tag = emit_enum_tag(pattern);
                out_ << "  " << matched << " = icmp eq i32 " << enum_tag
                     << ", " << pattern_tag << '\n';
            } else {
                const auto pattern = emit_expression(match_case.pattern);
                if (value.type == Type::integer || value.type == Type::boolean) {
                    out_ << "  " << matched << " = icmp eq " << llvm_type(value.type)
                         << ' ' << value.operand << ", " << pattern.operand << '\n';
                } else if (value.type == Type::number) {
                    out_ << "  " << matched << " = fcmp oeq double "
                         << value.operand << ", " << pattern.operand << '\n';
                } else if (value.type == Type::character) {
                    const auto same_length = temporary();
                    const auto same_bytes = temporary();
                    const auto bytes_equal = temporary();
                    out_ << "  " << same_length << " = icmp eq i64 "
                         << value.string_length << ", " << pattern.string_length << '\n'
                         << "  " << same_bytes << " = call i32 @memcmp(ptr "
                         << value.operand << ", ptr " << pattern.operand << ", i64 "
                         << value.string_length << ")\n"
                         << "  " << bytes_equal << " = icmp eq i32 " << same_bytes
                         << ", 0\n"
                         << "  " << matched << " = and i1 " << same_length << ", "
                         << bytes_equal << '\n';
                } else {
                    unsupported("scalar match type '" + cpp_type(value.type) + "'");
                }
            }
            const auto body_label = label("match.body");
            const auto next_label = label("match.next");
            out_ << "  br i1 " << matched << ", label %" << body_label
                 << ", label %" << next_label << '\n'
                 << body_label << ":\n";
            terminated_ = false;
            if (enum_value && !match_case.bindings.empty()) {
                const auto prefix = "SunType_" + value.type.name + "_";
                const auto variant_name = variant_type.substr(prefix.size());
                const auto& variants =
                    custom_types_.at(value.type.name)->enum_variants;
                const auto variant = std::find_if(
                    variants.begin(), variants.end(),
                    [&](const EnumVariant& candidate) {
                        return candidate.name == variant_name;
                    });
                if (variant == variants.end()) {
                    unsupported("unknown enum payload variant");
                }
                for (std::size_t i = 0; i < match_case.bindings.size(); ++i) {
                    const auto& binding = match_case.bindings[i];
                    const auto field = std::find_if(
                        variant->fields.begin(), variant->fields.end(),
                        [&](const Field& candidate) {
                            return candidate.name == binding.field_name;
                        });
                    if (field == variant->fields.end()) {
                        unsupported("unknown enum payload binding");
                    }
                    const auto slot_index = static_cast<std::size_t>(
                        std::distance(variant->fields.begin(), field));
                    const auto first_pointer = temporary();
                    const auto bits = temporary();
                    out_ << "  " << first_pointer
                         << " = getelementptr %sun.type." << value.type.name
                         << ", ptr " << value.operand
                         << ", i32 0, i32 1, i64 " << (slot_index * 2) << '\n'
                         << "  " << bits << " = load i64, ptr " << first_pointer
                         << '\n';
                    if (is_string_like(field->type)) {
                        const auto pointer = temporary();
                        const auto length_pointer = temporary();
                        const auto length = temporary();
                        out_ << "  " << pointer << " = inttoptr i64 " << bits
                             << " to ptr\n"
                             << "  " << length_pointer
                             << " = getelementptr %sun.type." << value.type.name
                             << ", ptr " << value.operand
                             << ", i32 0, i32 1, i64 " << (slot_index * 2 + 1)
                             << '\n'
                             << "  " << length << " = load i64, ptr "
                             << length_pointer << '\n'
                             << "  store ptr " << pointer << ", ptr %sun.slot."
                             << binding.cpp_name << '\n'
                             << "  store i64 " << length << ", ptr %sun.len."
                             << binding.cpp_name << '\n';
                    } else if (field->type == Type::number) {
                        const auto number = temporary();
                        out_ << "  " << number << " = bitcast i64 " << bits
                             << " to double\n"
                             << "  store double " << number << ", ptr %sun.slot."
                             << binding.cpp_name << '\n';
                    } else if (field->type == Type::boolean) {
                        const auto boolean = temporary();
                        out_ << "  " << boolean << " = trunc i64 " << bits
                             << " to i1\n"
                             << "  store i1 " << boolean << ", ptr %sun.slot."
                             << binding.cpp_name << '\n';
                    } else {
                        out_ << "  store i64 " << bits << ", ptr %sun.slot."
                             << binding.cpp_name << '\n';
                    }
                }
            }
            emit_block(match_case.body);
            if (!terminated_) out_ << "  br label %" << end_label << '\n';
            out_ << next_label << ":\n";
            terminated_ = false;
        }
        if (statement.else_body) {
            emit_block(*statement.else_body);
            if (!terminated_) out_ << "  br label %" << end_label << '\n';
        } else {
            out_ << "  br label %" << end_label << '\n';
        }
        out_ << end_label << ":\n";
        terminated_ = false;
    }

    void emit_if(const IfStatement& statement) {
        const auto end_label = label("if.end");
        for (const auto& branch : statement.branches) {
            const auto condition = emit_expression(branch.condition);
            const auto body_label = label("if.body");
            const auto next_label = label("if.next");
            out_ << "  br i1 " << condition.operand << ", label %" << body_label
                 << ", label %" << next_label << '\n'
                 << body_label << ":\n";
            terminated_ = false;
            emit_block(branch.body);
            if (!terminated_) out_ << "  br label %" << end_label << '\n';
            out_ << next_label << ":\n";
            terminated_ = false;
        }
        if (statement.else_body) {
            emit_block(*statement.else_body);
            if (!terminated_) out_ << "  br label %" << end_label << '\n';
        } else {
            out_ << "  br label %" << end_label << '\n';
        }
        out_ << end_label << ":\n";
        terminated_ = false;
    }

    void emit_range_for(const RangeForStatement& statement) {
        const auto start = emit_expression(statement.start_value);
        const auto stop = emit_expression(statement.stop_value);
        const auto step = emit_expression(statement.step_value);
        out_ << "  store double " << start.operand << ", ptr %sun.slot."
             << statement.variable_name << '\n';

        const auto step_zero_fail = label("range.zero.fail");
        const auto step_zero_ok = label("range.zero.ok");
        const auto condition_label = label("range.cond");
        const auto body_label = label("range.body");
        const auto increment_label = label("range.increment");
        const auto end_label = label("range.end");

        const auto step_zero = temporary();
        out_ << "  " << step_zero << " = fcmp oeq double " << step.operand
             << ", 0.0\n"
             << "  br i1 " << step_zero << ", label %" << step_zero_fail
             << ", label %" << step_zero_ok << '\n'
             << step_zero_fail << ":\n"
             << "  call void @sun_llvm_range_step_zero()\n"
             << "  unreachable\n"
             << step_zero_ok << ":\n"
             << "  br label %" << condition_label << '\n'
             << condition_label << ":\n";

        const auto variable = temporary();
        out_ << "  " << variable << " = load double, ptr %sun.slot."
             << statement.variable_name << '\n';
        const auto positive = temporary();
        const auto positive_condition = temporary();
        const auto negative_condition = temporary();
        out_ << "  " << positive << " = fcmp ogt double " << step.operand
             << ", 0.0\n"
             << "  " << positive_condition << " = fcmp olt double "
             << variable << ", " << stop.operand << '\n'
             << "  " << negative_condition << " = fcmp ogt double "
             << variable << ", " << stop.operand << '\n';

        const auto selected = temporary();
        out_ << "  " << selected << " = select i1 " << positive
             << ", i1 " << positive_condition << ", i1 " << negative_condition
             << '\n'
             << "  br i1 " << selected << ", label %" << body_label
             << ", label %" << end_label << '\n'
             << body_label << ":\n";

        terminated_ = false;
        loop_targets_.emplace_back(end_label, increment_label);
        emit_block(statement.body);
        loop_targets_.pop_back();

        if (!terminated_) {
            out_ << "  br label %" << increment_label << '\n';
        }
        out_ << increment_label << ":\n";
        const auto current = temporary();
        const auto next = temporary();
        out_ << "  " << current << " = load double, ptr %sun.slot."
             << statement.variable_name << '\n'
             << "  " << next << " = fadd double " << current << ", "
             << step.operand << '\n'
             << "  store double " << next << ", ptr %sun.slot."
             << statement.variable_name << '\n'
             << "  br label %" << condition_label << '\n';
        out_ << end_label << ":\n";
        terminated_ = false;
    }

    void emit_for_each(const ForEachStatement& statement) {
        if (statement.collection.type == Type::bytes) {
            const auto collection = emit_expression(statement.collection);
            const auto index_slot = temporary();
            out_ << "  " << index_slot << " = alloca i64\n"
                 << "  store i64 0, ptr " << index_slot << '\n';
            const auto condition_label = label("foreach.bytes.cond");
            const auto body_label = label("foreach.bytes.body");
            const auto increment_label = label("foreach.bytes.increment");
            const auto end_label = label("foreach.bytes.end");
            out_ << "  br label %" << condition_label << '\n'
                 << condition_label << ":\n";
            const auto index = temporary();
            out_ << "  " << index << " = load i64, ptr " << index_slot << '\n';
            const auto length = temporary();
            out_ << "  " << length << " = call i64 @sun_llvm_bytes_len(ptr "
                 << collection.operand << ")\n";
            const auto more = temporary();
            out_ << "  " << more << " = icmp slt i64 " << index << ", " << length << '\n'
                 << "  br i1 " << more << ", label %" << body_label
                 << ", label %" << end_label << '\n'
                 << body_label << ":\n";
            const auto index_double = temporary();
            out_ << "  " << index_double << " = sitofp i64 " << index
                 << " to double\n";
            const auto item = temporary();
            out_ << "  " << item << " = call i64 @sun_llvm_bytes_at(ptr "
                 << collection.operand << ", double " << index_double << ")\n"
                 << "  store i64 " << item << ", ptr %sun.slot."
                 << statement.variable_name << '\n';
            terminated_ = false;
            loop_targets_.emplace_back(end_label, increment_label);
            emit_block(statement.body);
            loop_targets_.pop_back();
            if (!terminated_) out_ << "  br label %" << increment_label << '\n';
            out_ << increment_label << ":\n";
            const auto next = temporary();
            out_ << "  " << next << " = add i64 " << index << ", 1\n"
                 << "  store i64 " << next << ", ptr " << index_slot << '\n'
                 << "  br label %" << condition_label << '\n'
                 << end_label << ":\n";
            terminated_ = false;
            return;
        }
        Expression iterable = statement.collection;
        if (statement.dictionary && is_dictionary_type(statement.collection.type)) {
            const auto key_type = dictionary_key_type(statement.collection.type);
            iterable.kind = Expression::Kind::call;
            iterable.type = llvm_list_type(key_type);
            iterable.value = "sun_dict_keys";
            iterable.children = {statement.collection};
        }
        if (!is_list_type(iterable.type)) {
            unsupported("for-in over non-list collections");
        }
        const auto collection = emit_expression(iterable);
        const auto item_type = list_item_type(iterable.type);
        const auto suffix = llvm_list_suffix(item_type);

        const auto index_slot = temporary();
        const auto index_start = temporary();
        const auto index_alloca = "%" + index_slot.substr(1) + ".slot";
        out_ << "  " << index_start << " = alloca i64\n"
             << "  store i64 0, ptr " << index_start << '\n';

        const auto condition_label = label("foreach.cond");
        const auto body_label = label("foreach.body");
        const auto increment_label = label("foreach.increment");
        const auto end_label = label("foreach.end");

        out_ << "  br label %" << condition_label << '\n'
             << condition_label << ":\n";
        const auto index = temporary();
        out_ << "  " << index << " = load i64, ptr " << index_start << '\n';
        const auto length = temporary();
        out_ << "  " << length << " = call i64 @sun_llvm_list_len_" << suffix
             << "(ptr " << collection.operand << ")\n";
        const auto more = temporary();
        out_ << "  " << more << " = icmp slt i64 " << index << ", " << length << '\n'
             << "  br i1 " << more << ", label %" << body_label
             << ", label %" << end_label << '\n'
             << body_label << ":\n";

        const auto index_number = temporary();
        out_ << "  " << index_number << " = sitofp i64 " << index << " to double\n";
        if (is_string_like(item_type)) {
            const auto length_slot = temporary();
            out_ << "  " << length_slot << " = alloca i64\n";
            const auto item = temporary();
            out_ << "  " << item << " = call ptr @sun_llvm_list_at_string(ptr "
                 << collection.operand << ", double " << index_number
                 << ", ptr " << length_slot << ")\n";
            const auto item_length = temporary();
            out_ << "  " << item_length << " = load i64, ptr " << length_slot << '\n'
                 << "  store ptr " << item << ", ptr %sun.slot."
                 << statement.variable_name << '\n'
                 << "  store i64 " << item_length << ", ptr %sun.len."
                 << statement.variable_name << '\n';
        } else if (item_type.kind == Type::Kind::tuple) {
            const auto item_pointer = temporary();
            const auto item = temporary();
            out_ << "  " << item_pointer << " = call ptr @sun_llvm_list_at_ptr(ptr "
                 << collection.operand << ", double " << index_number << ")\n"
                 << "  " << item << " = load " << llvm_type(item_type)
                 << ", ptr " << item_pointer << '\n'
                 << "  store " << llvm_type(item_type) << ' ' << item
                 << ", ptr %sun.slot." << statement.variable_name << '\n';
        } else {
            const auto item = temporary();
            out_ << "  " << item << " = call " << llvm_type(item_type)
                 << " @sun_llvm_list_at_" << suffix << "(ptr "
                 << collection.operand << ", double " << index_number << ")\n"
                 << "  store " << llvm_type(item_type) << " " << item
                 << ", ptr %sun.slot." << statement.variable_name << '\n';
        }

        terminated_ = false;
        loop_targets_.emplace_back(end_label, increment_label);
        emit_block(statement.body);
        loop_targets_.pop_back();
        if (!terminated_) {
            out_ << "  br label %" << increment_label << '\n';
        }
        out_ << increment_label << ":\n";
        const auto next_index = temporary();
        out_ << "  " << next_index << " = add i64 " << index << ", 1\n"
             << "  store i64 " << next_index << ", ptr " << index_start << '\n'
             << "  br label %" << condition_label << '\n'
             << end_label << ":\n";
        terminated_ = false;
    }

    void emit_while(const WhileStatement& statement) {
        const auto condition_label = label("while.cond");
        const auto body_label = label("while.body");
        const auto end_label = label("while.end");
        out_ << "  br label %" << condition_label << '\n'
             << condition_label << ":\n";
        terminated_ = false;
        const auto condition = emit_expression(statement.condition);
        out_ << "  br i1 " << condition.operand << ", label %" << body_label
             << ", label %" << end_label << '\n'
             << body_label << ":\n";
        terminated_ = false;
        loop_targets_.emplace_back(end_label, condition_label);
        emit_block(statement.body);
        loop_targets_.pop_back();
        if (!terminated_) out_ << "  br label %" << condition_label << '\n';
        out_ << end_label << ":\n";
        terminated_ = false;
    }

    std::ostringstream out_;
    std::vector<std::pair<std::string, std::string>> strings_;
    std::unordered_map<std::string, Type> variables_;
    std::unordered_map<std::string, const Function*> functions_;
    std::unordered_map<std::string, const Function*> methods_;
    std::unordered_map<std::string, const CustomTypeDefinition*> custom_types_;
    std::unordered_map<std::string, LocalFunctionInfo> local_functions_;
    std::vector<std::string> local_function_order_;
    std::unordered_set<std::string> custom_error_types_;
    std::unordered_map<std::string, std::string> capture_slots_;
    std::unordered_map<std::string, std::string> capture_lengths_;
    std::vector<std::pair<std::string, std::string>> loop_targets_;
    std::vector<std::pair<const Block*, std::string>> finally_stack_;
    std::string exception_target_;
    std::string current_owner_type_;
    std::size_t next_temp_ = 0;
    std::size_t next_label_ = 0;
    bool terminated_ = false;
};

} // namespace

std::string transpile_llvm(const Program& program) {
    return Generator{}.generate(program);
}

} // namespace sun
