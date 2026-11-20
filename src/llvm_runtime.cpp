#include <algorithm>
#include <cctype>
#include <cmath>
#include <cxxabi.h>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <unordered_map>
#include <vector>

extern "C" const char* sun_llvm_catch_exception(void* exception_object,
                                                 std::int64_t* length);

namespace {

struct SunStringList {
    std::vector<std::string> values;
};

struct SunIntList {
    std::vector<std::int64_t> values;
};

struct SunNumberList {
    std::vector<double> values;
};

struct SunBoolList {
    std::vector<bool> values;
};

struct SunPointerList {
    std::vector<void*> values;
};

bool sun_llvm_is_one_utf8_character(const std::string& value);

struct SunLLVMErrorValue {
    std::string type_name;
    std::string message;
};

class SunLLVMCustomException final : public std::exception {
public:
    SunLLVMCustomException(std::string type_name, std::string message)
        : type_name_(std::move(type_name)), message_(std::move(message)) {}

    const char* what() const noexcept override { return message_.c_str(); }
    const std::string& type_name() const noexcept { return type_name_; }

private:
    std::string type_name_;
    std::string message_;
};

std::size_t sun_list_index(std::size_t size, double index) {
    if (!std::isfinite(index) || index < 0.0 || std::floor(index) != index ||
        index >= static_cast<double>(size)) {
        throw std::out_of_range("list index out of bounds");
    }
    return static_cast<std::size_t>(index);
}

std::size_t sun_list_insert_index(std::size_t size, double index) {
    if (!std::isfinite(index) || index < 0.0 || std::floor(index) != index ||
        index > static_cast<double>(size)) {
        throw std::out_of_range("list insertion index out of bounds");
    }
    return static_cast<std::size_t>(index);
}

const char* sun_llvm_store_string(std::string value, std::int64_t* length) {
    auto* stored = new std::string(std::move(value));
    *length = static_cast<std::int64_t>(stored->size());
    return stored->data();
}

} // namespace

extern "C" const char* sun_llvm_catch_exception(void* exception_object,
                                                 std::int64_t* length) {
    auto* caught = static_cast<const std::exception*>(
        __cxxabiv1::__cxa_begin_catch(exception_object));
    thread_local std::string message;
    message = caught ? caught->what() : "unknown runtime error";
    *length = static_cast<std::int64_t>(message.size());
    __cxxabiv1::__cxa_end_catch();
    return message.data();
}

extern "C" bool sun_llvm_exception_matches(void* exception_object,
                                             const char* type_name,
                                             std::int64_t type_name_length) noexcept {
    auto* caught = static_cast<const std::exception*>(
        __cxxabiv1::__cxa_get_exception_ptr(exception_object));
    const auto* custom = dynamic_cast<const SunLLVMCustomException*>(caught);
    if (!custom) return false;
    const auto& actual_type = custom->type_name();
    const auto expected_length = static_cast<std::size_t>(type_name_length);
    return actual_type.size() == expected_length &&
           std::memcmp(actual_type.data(), type_name, expected_length) == 0;
}

extern "C" void* sun_llvm_error_create(const char* type_name,
                                         std::int64_t type_name_length,
                                         const char* message,
                                         std::int64_t message_length) {
    return new SunLLVMErrorValue{
        std::string(type_name, static_cast<std::size_t>(type_name_length)),
        std::string(message, static_cast<std::size_t>(message_length))};
}

extern "C" void sun_llvm_raise_custom(void* error_value) {
    auto* error = static_cast<SunLLVMErrorValue*>(error_value);
    SunLLVMCustomException exception(error->type_name, error->message);
    delete error;
    throw exception;
}

extern "C" [[noreturn]] void sun_llvm_rethrow_exception(void* exception_object) {
    __cxxabiv1::__cxa_begin_catch(exception_object);
    __cxxabiv1::__cxa_rethrow();
}

extern "C" void sun_llvm_tuple_index_missing() {
    throw std::out_of_range("tuple.index: value not found");
}

extern "C" std::int64_t sun_llvm_checked_add(std::int64_t left,
                                               std::int64_t right) {
    if ((right > 0 && left > std::numeric_limits<std::int64_t>::max() - right) ||
        (right < 0 && left < std::numeric_limits<std::int64_t>::min() - right)) {
        throw std::overflow_error("int arithmetic overflow");
    }
    return left + right;
}

extern "C" std::int64_t sun_llvm_checked_sub(std::int64_t left,
                                               std::int64_t right) {
    if ((right < 0 && left > std::numeric_limits<std::int64_t>::max() + right) ||
        (right > 0 && left < std::numeric_limits<std::int64_t>::min() + right)) {
        throw std::overflow_error("int arithmetic overflow");
    }
    return left - right;
}

extern "C" std::int64_t sun_llvm_checked_mul(std::int64_t left,
                                               std::int64_t right) {
    if (left != 0 && right != 0) {
        const auto min = std::numeric_limits<std::int64_t>::min();
        const auto max = std::numeric_limits<std::int64_t>::max();
        if ((left == -1 && right == min) || (right == -1 && left == min) ||
            (left > 0 && ((right > 0 && left > max / right) ||
                          (right < 0 && right < min / left))) ||
            (left < 0 && ((right > 0 && left < min / right) ||
                          (right < 0 && left < max / right)))) {
            throw std::overflow_error("int arithmetic overflow");
        }
    }
    return left * right;
}

extern "C" std::int64_t sun_llvm_checked_div(std::int64_t left,
                                               std::int64_t right) {
    if (right == 0) throw std::runtime_error("int division by zero");
    if (left == std::numeric_limits<std::int64_t>::min() && right == -1) {
        throw std::overflow_error("int arithmetic overflow");
    }
    return left / right;
}

extern "C" std::int64_t sun_llvm_checked_mod(std::int64_t left,
                                               std::int64_t right) {
    if (right == 0) throw std::runtime_error("int modulo by zero");
    if (left == std::numeric_limits<std::int64_t>::min() && right == -1) return 0;
    return left % right;
}

extern "C" std::int64_t sun_llvm_checked_neg(std::int64_t value) {
    if (value == std::numeric_limits<std::int64_t>::min()) {
        throw std::overflow_error("int arithmetic overflow");
    }
    return -value;
}

extern "C" std::int64_t sun_llvm_checked_number_to_int(double value) {
    const double limit = std::ldexp(1.0, 63);
    if (!std::isfinite(value) || std::floor(value) != value ||
        value < -limit || value >= limit) {
        throw std::runtime_error("int() requires an in-range whole number");
    }
    return static_cast<std::int64_t>(value);
}

extern "C" const char* sun_llvm_checked_char(const char* value,
                                              std::int64_t length) {
    const std::string text(value, static_cast<std::size_t>(length));
    if (!sun_llvm_is_one_utf8_character(text)) {
        throw std::runtime_error("char() requires one UTF-8 character");
    }
    return value;
}

extern "C" void sun_llvm_range_step_zero() {
    throw std::runtime_error("range step cannot be zero");
}

extern "C" void* sun_llvm_list_create_i64() { return new SunIntList{}; }
extern "C" void* sun_llvm_list_create_double() { return new SunNumberList{}; }
extern "C" void* sun_llvm_list_create_bool() { return new SunBoolList{}; }
extern "C" void* sun_llvm_list_create_string() { return new SunStringList{}; }
extern "C" void* sun_llvm_list_create_ptr() { return new SunPointerList{}; }

extern "C" void sun_llvm_list_append_i64(void* list, std::int64_t value) {
    static_cast<SunIntList*>(list)->values.push_back(value);
}
extern "C" void sun_llvm_list_append_double(void* list, double value) {
    static_cast<SunNumberList*>(list)->values.push_back(value);
}
extern "C" void sun_llvm_list_append_bool(void* list, bool value) {
    static_cast<SunBoolList*>(list)->values.push_back(value);
}
extern "C" void sun_llvm_list_append_string(void* list, const char* value, std::int64_t length) {
    static_cast<SunStringList*>(list)->values.emplace_back(value, static_cast<std::size_t>(length));
}
extern "C" void sun_llvm_list_append_ptr(void* list, void* value) {
    static_cast<SunPointerList*>(list)->values.push_back(value);
}

extern "C" std::int64_t sun_llvm_list_at_i64(void* list, double index) {
    auto& values = static_cast<SunIntList*>(list)->values;
    return values[sun_list_index(values.size(), index)];
}
extern "C" double sun_llvm_list_at_double(void* list, double index) {
    auto& values = static_cast<SunNumberList*>(list)->values;
    return values[sun_list_index(values.size(), index)];
}
extern "C" bool sun_llvm_list_at_bool(void* list, double index) {
    auto& values = static_cast<SunBoolList*>(list)->values;
    return values[sun_list_index(values.size(), index)];
}
extern "C" const char* sun_llvm_list_at_string(void* list, double index, std::int64_t* length) {
    auto& values = static_cast<SunStringList*>(list)->values;
    const auto& value = values[sun_list_index(values.size(), index)];
    *length = static_cast<std::int64_t>(value.size());
    return value.data();
}
extern "C" void* sun_llvm_list_at_ptr(void* list, double index) {
    auto& values = static_cast<SunPointerList*>(list)->values;
    return values[sun_list_index(values.size(), index)];
}

extern "C" std::int64_t sun_llvm_list_len_i64(void* list) {
    return static_cast<std::int64_t>(static_cast<SunIntList*>(list)->values.size());
}
extern "C" std::int64_t sun_llvm_list_len_double(void* list) {
    return static_cast<std::int64_t>(static_cast<SunNumberList*>(list)->values.size());
}
extern "C" std::int64_t sun_llvm_list_len_bool(void* list) {
    return static_cast<std::int64_t>(static_cast<SunBoolList*>(list)->values.size());
}
extern "C" std::int64_t sun_llvm_list_len_string(void* list) {
    return static_cast<std::int64_t>(static_cast<SunStringList*>(list)->values.size());
}
extern "C" std::int64_t sun_llvm_list_len_ptr(void* list) {
    return static_cast<std::int64_t>(static_cast<SunPointerList*>(list)->values.size());
}

extern "C" void sun_llvm_list_clear_i64(void* list) { static_cast<SunIntList*>(list)->values.clear(); }
extern "C" void sun_llvm_list_clear_double(void* list) { static_cast<SunNumberList*>(list)->values.clear(); }
extern "C" void sun_llvm_list_clear_bool(void* list) { static_cast<SunBoolList*>(list)->values.clear(); }
extern "C" void sun_llvm_list_clear_string(void* list) { static_cast<SunStringList*>(list)->values.clear(); }
extern "C" void sun_llvm_list_clear_ptr(void* list) { static_cast<SunPointerList*>(list)->values.clear(); }

extern "C" bool sun_llvm_list_contains_i64(void* list, std::int64_t value) {
    auto& values = static_cast<SunIntList*>(list)->values;
    for (auto item : values) if (item == value) return true;
    return false;
}
extern "C" bool sun_llvm_list_contains_double(void* list, double value) {
    auto& values = static_cast<SunNumberList*>(list)->values;
    for (auto item : values) if (item == value) return true;
    return false;
}
extern "C" bool sun_llvm_list_contains_bool(void* list, bool value) {
    auto& values = static_cast<SunBoolList*>(list)->values;
    for (auto item : values) if (item == value) return true;
    return false;
}
extern "C" bool sun_llvm_list_contains_string(void* list, const char* value, std::int64_t length) {
    const std::string needle(value, static_cast<std::size_t>(length));
    auto& values = static_cast<SunStringList*>(list)->values;
    for (const auto& item : values) if (item == needle) return true;
    return false;
}
extern "C" bool sun_llvm_list_contains_ptr(void* list, void* value) {
    const auto& values = static_cast<SunPointerList*>(list)->values;
    return std::find(values.begin(), values.end(), value) != values.end();
}

extern "C" void sun_llvm_list_insert_i64(void* list, double index, std::int64_t value) {
    auto& values = static_cast<SunIntList*>(list)->values;
    values.insert(values.begin() + sun_list_insert_index(values.size(), index), value);
}
extern "C" void sun_llvm_list_insert_double(void* list, double index, double value) {
    auto& values = static_cast<SunNumberList*>(list)->values;
    values.insert(values.begin() + sun_list_insert_index(values.size(), index), value);
}
extern "C" void sun_llvm_list_insert_bool(void* list, double index, bool value) {
    auto& values = static_cast<SunBoolList*>(list)->values;
    values.insert(values.begin() + sun_list_insert_index(values.size(), index), value);
}
extern "C" void sun_llvm_list_insert_string(void* list, double index, const char* value, std::int64_t length) {
    auto& values = static_cast<SunStringList*>(list)->values;
    values.insert(values.begin() + sun_list_insert_index(values.size(), index),
                  std::string(value, static_cast<std::size_t>(length)));
}
extern "C" void sun_llvm_list_insert_ptr(void* list, double index, void* value) {
    auto& values = static_cast<SunPointerList*>(list)->values;
    values.insert(values.begin() + sun_list_insert_index(values.size(), index), value);
}

extern "C" bool sun_llvm_list_remove_i64(void* list, std::int64_t value) {
    auto& values = static_cast<SunIntList*>(list)->values;
    for (auto it = values.begin(); it != values.end(); ++it) if (*it == value) { values.erase(it); return true; }
    return false;
}
extern "C" bool sun_llvm_list_remove_double(void* list, double value) {
    auto& values = static_cast<SunNumberList*>(list)->values;
    for (auto it = values.begin(); it != values.end(); ++it) if (*it == value) { values.erase(it); return true; }
    return false;
}
extern "C" bool sun_llvm_list_remove_bool(void* list, bool value) {
    auto& values = static_cast<SunBoolList*>(list)->values;
    for (auto it = values.begin(); it != values.end(); ++it) if (*it == value) { values.erase(it); return true; }
    return false;
}
extern "C" bool sun_llvm_list_remove_string(void* list, const char* value, std::int64_t length) {
    const std::string needle(value, static_cast<std::size_t>(length));
    auto& values = static_cast<SunStringList*>(list)->values;
    for (auto it = values.begin(); it != values.end(); ++it) if (*it == needle) { values.erase(it); return true; }
    return false;
}
extern "C" bool sun_llvm_list_remove_ptr(void* list, void* value) {
    auto& values = static_cast<SunPointerList*>(list)->values;
    for (auto it = values.begin(); it != values.end(); ++it) {
        if (*it == value) {
            values.erase(it);
            return true;
        }
    }
    return false;
}

extern "C" std::int64_t sun_llvm_list_pop_i64(void* list, double index) {
    auto& values = static_cast<SunIntList*>(list)->values;
    auto pos = sun_list_index(values.size(), index);
    auto value = values[pos]; values.erase(values.begin() + pos); return value;
}
extern "C" double sun_llvm_list_pop_double(void* list, double index) {
    auto& values = static_cast<SunNumberList*>(list)->values;
    auto pos = sun_list_index(values.size(), index);
    auto value = values[pos]; values.erase(values.begin() + pos); return value;
}
extern "C" bool sun_llvm_list_pop_bool(void* list, double index) {
    auto& values = static_cast<SunBoolList*>(list)->values;
    auto pos = sun_list_index(values.size(), index);
    auto value = values[pos]; values.erase(values.begin() + pos); return value;
}
extern "C" void* sun_llvm_list_pop_ptr(void* list, double index) {
    auto& values = static_cast<SunPointerList*>(list)->values;
    auto pos = sun_list_index(values.size(), index);
    auto value = values[pos]; values.erase(values.begin() + pos); return value;
}
extern "C" void sun_llvm_list_set_i64(void* list, double index, std::int64_t value) {
    auto& values = static_cast<SunIntList*>(list)->values;
    values[sun_list_index(values.size(), index)] = value;
}
extern "C" void sun_llvm_list_set_double(void* list, double index, double value) {
    auto& values = static_cast<SunNumberList*>(list)->values;
    values[sun_list_index(values.size(), index)] = value;
}
extern "C" void sun_llvm_list_set_bool(void* list, double index, bool value) {
    auto& values = static_cast<SunBoolList*>(list)->values;
    values[sun_list_index(values.size(), index)] = value;
}
extern "C" void sun_llvm_list_set_string(void* list, double index, const char* value, std::int64_t length) {
    auto& values = static_cast<SunStringList*>(list)->values;
    values[sun_list_index(values.size(), index)] = std::string(value, static_cast<std::size_t>(length));
}
extern "C" void sun_llvm_list_set_ptr(void* list, double index, void* value) {
    auto& values = static_cast<SunPointerList*>(list)->values;
    values[sun_list_index(values.size(), index)] = value;
}

struct SunBytes {
    std::vector<std::uint8_t> values;
};

extern "C" void sun_llvm_assert(bool condition) {
    if (!condition) throw std::runtime_error("assertion failed");
}

extern "C" void sun_llvm_raise(const char* message, std::int64_t length) {
    throw std::runtime_error(std::string(message, static_cast<std::size_t>(length)));
}

extern "C" void* sun_llvm_bytes_create() { return new SunBytes{}; }
extern "C" void* sun_llvm_bytes_from_list(void* list) {
    auto* bytes = new SunBytes{};
    for (const auto value : static_cast<SunIntList*>(list)->values) {
        if (value < 0 || value > 255) throw std::out_of_range("byte value out of range");
        bytes->values.push_back(static_cast<std::uint8_t>(value));
    }
    return bytes;
}
extern "C" void* sun_llvm_bytes_to_list(void* bytes) {
    auto* result = new SunIntList{};
    for (const auto value : static_cast<SunBytes*>(bytes)->values) {
        result->values.push_back(value);
    }
    return result;
}
extern "C" std::int64_t sun_llvm_bytes_len(void* bytes) {
    return static_cast<std::int64_t>(static_cast<SunBytes*>(bytes)->values.size());
}
extern "C" std::int64_t sun_llvm_bytes_at(void* bytes, double index) {
    auto& values = static_cast<SunBytes*>(bytes)->values;
    if (!std::isfinite(index) || index < 0.0 || std::floor(index) != index ||
        index >= static_cast<double>(values.size())) {
        throw std::out_of_range("bytes index out of bounds");
    }
    return values[static_cast<std::size_t>(index)];
}
extern "C" void sun_llvm_bytes_set(void* bytes, double index, std::int64_t value) {
    auto& values = static_cast<SunBytes*>(bytes)->values;
    if (!std::isfinite(index) || index < 0.0 || std::floor(index) != index ||
        index >= static_cast<double>(values.size())) throw std::out_of_range("bytes index out of bounds");
    if (value < 0 || value > 255) throw std::out_of_range("byte value out of range");
    values[static_cast<std::size_t>(index)] = static_cast<std::uint8_t>(value);
}
extern "C" void sun_llvm_bytes_append(void* bytes, std::int64_t value) {
    if (value < 0 || value > 255) throw std::out_of_range("byte value out of range");
    static_cast<SunBytes*>(bytes)->values.push_back(static_cast<std::uint8_t>(value));
}
extern "C" bool sun_llvm_bytes_contains(void* bytes, std::int64_t value) {
    if (value < 0 || value > 255) return false;
    auto& values = static_cast<SunBytes*>(bytes)->values;
    return std::find(values.begin(), values.end(), static_cast<std::uint8_t>(value)) != values.end();
}
extern "C" void sun_llvm_bytes_clear(void* bytes) {
    static_cast<SunBytes*>(bytes)->values.clear();
}
extern "C" void* sun_llvm_bytes_from_string(const char* value, std::int64_t length) {
    auto* bytes = new SunBytes{};
    const auto* begin = reinterpret_cast<const std::uint8_t*>(value);
    bytes->values.assign(begin, begin + static_cast<std::size_t>(length));
    return bytes;
}
extern "C" const char* sun_llvm_bytes_to_string(void* bytes, std::int64_t* length) {
    auto& values = static_cast<SunBytes*>(bytes)->values;
    auto* stored = new std::string(reinterpret_cast<const char*>(values.data()), values.size());
    *length = static_cast<std::int64_t>(stored->size());
    return stored->data();
}

extern "C" const char* sun_llvm_string_concat(const char* left,
                                              std::int64_t left_length,
                                              const char* right,
                                              std::int64_t right_length,
                                              std::int64_t* result_length) {
    std::string result(left, static_cast<std::size_t>(left_length));
    result.append(right, static_cast<std::size_t>(right_length));
    return sun_llvm_store_string(std::move(result), result_length);
}

extern "C" const char* sun_llvm_string_upper(const char* value,
                                             std::int64_t length,
                                             std::int64_t* result_length) {
    std::string result(value, static_cast<std::size_t>(length));
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char ch) {
                       return static_cast<char>(std::toupper(ch));
                   });
    return sun_llvm_store_string(std::move(result), result_length);
}

extern "C" const char* sun_llvm_string_lower(const char* value,
                                             std::int64_t length,
                                             std::int64_t* result_length) {
    std::string result(value, static_cast<std::size_t>(length));
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char ch) {
                       return static_cast<char>(std::tolower(ch));
                   });
    return sun_llvm_store_string(std::move(result), result_length);
}

extern "C" const char* sun_llvm_string_trim(const char* value,
                                             std::int64_t length,
                                             std::int64_t* result_length) {
    const std::string input(value, static_cast<std::size_t>(length));
    const auto is_space = [](unsigned char ch) { return std::isspace(ch) != 0; };
    const auto first = std::find_if_not(input.begin(), input.end(), is_space);
    const auto last = std::find_if_not(input.rbegin(), input.rend(), is_space).base();
    return sun_llvm_store_string(first < last ? std::string(first, last)
                                               : std::string{},
                                 result_length);
}

extern "C" void* sun_llvm_string_split(const char* value,
                                        std::int64_t length,
                                        const char* separator,
                                        std::int64_t separator_length) {
    if (separator_length == 0) {
        throw std::invalid_argument("string.split separator cannot be empty");
    }
    const std::string input(value, static_cast<std::size_t>(length));
    const std::string delimiter(separator,
                                static_cast<std::size_t>(separator_length));
    auto* result = new SunStringList{};
    std::size_t start = 0;
    while (true) {
        const auto next = input.find(delimiter, start);
        if (next == std::string::npos) {
            result->values.push_back(input.substr(start));
            break;
        }
        result->values.push_back(input.substr(start, next - start));
        start = next + delimiter.size();
    }
    return result;
}

extern "C" const char* sun_llvm_string_replace(const char* value,
                                                std::int64_t length,
                                                const char* old_value,
                                                std::int64_t old_length,
                                                const char* new_value,
                                                std::int64_t new_length,
                                                std::int64_t* result_length) {
    if (old_length == 0) {
        throw std::invalid_argument("string.replace search text cannot be empty");
    }
    const std::string input(value, static_cast<std::size_t>(length));
    const std::string search(old_value, static_cast<std::size_t>(old_length));
    const std::string replacement(new_value, static_cast<std::size_t>(new_length));
    std::string result;
    std::size_t start = 0;
    while (true) {
        const auto next = input.find(search, start);
        if (next == std::string::npos) {
            result.append(input, start, std::string::npos);
            break;
        }
        result.append(input, start, next - start);
        result.append(replacement);
        start = next + search.size();
    }
    return sun_llvm_store_string(std::move(result), result_length);
}

extern "C" bool sun_llvm_string_contains(const char* value,
                                          std::int64_t length,
                                          const char* part,
                                          std::int64_t part_length) {
    return std::string(value, static_cast<std::size_t>(length)).find(
               std::string(part, static_cast<std::size_t>(part_length))) !=
           std::string::npos;
}

extern "C" std::int64_t sun_llvm_string_count(const char* value,
                                               std::int64_t length,
                                               const char* part,
                                               std::int64_t part_length) {
    if (part_length == 0) {
        throw std::invalid_argument("string.count search text cannot be empty");
    }
    const std::string input(value, static_cast<std::size_t>(length));
    const std::string search(part, static_cast<std::size_t>(part_length));
    std::int64_t count = 0;
    for (std::size_t position = 0;
         (position = input.find(search, position)) != std::string::npos;
         position += search.size()) {
        if (count == std::numeric_limits<std::int64_t>::max()) {
            throw std::overflow_error("string.count result exceeds int range");
        }
        ++count;
    }
    return count;
}

extern "C" bool sun_llvm_string_starts_with(const char* value,
                                             std::int64_t length,
                                             const char* prefix,
                                             std::int64_t prefix_length) {
    return length >= prefix_length &&
           std::memcmp(value, prefix, static_cast<std::size_t>(prefix_length)) == 0;
}

extern "C" bool sun_llvm_string_ends_with(const char* value,
                                            std::int64_t length,
                                            const char* suffix,
                                            std::int64_t suffix_length) {
    return length >= suffix_length &&
           std::memcmp(value + length - suffix_length, suffix,
                       static_cast<std::size_t>(suffix_length)) == 0;
}

extern "C" std::int64_t sun_llvm_list_sum_i64(void* list) {
    std::int64_t total = 0;
    for (const auto value : static_cast<SunIntList*>(list)->values) {
        if ((value > 0 && total > std::numeric_limits<std::int64_t>::max() - value) ||
            (value < 0 && total < std::numeric_limits<std::int64_t>::min() - value)) {
            throw std::overflow_error("int arithmetic overflow");
        }
        total += value;
    }
    return total;
}

extern "C" double sun_llvm_list_sum_double(void* list) {
    double total = 0.0;
    for (const auto value : static_cast<SunNumberList*>(list)->values) {
        total += value;
    }
    return total;
}

extern "C" bool sun_llvm_list_all_bool(void* list) {
    for (const auto value : static_cast<SunBoolList*>(list)->values) {
        if (!value) return false;
    }
    return true;
}

extern "C" bool sun_llvm_list_any_bool(void* list) {
    for (const auto value : static_cast<SunBoolList*>(list)->values) {
        if (value) return true;
    }
    return false;
}

extern "C" double sun_llvm_round_digits(double value, std::int64_t digits) {
    if (digits > 308) return value;
    if (digits < -308) {
        return std::isfinite(value) ? std::copysign(0.0, value) : value;
    }
    const double factor = std::pow(10.0, static_cast<double>(digits));
    return std::round(value * factor) / factor;
}

extern "C" double sun_llvm_math_factorial(double value) {
    if (!std::isfinite(value)) {
        throw std::runtime_error("math.factorial expects a finite number");
    }
    const auto result = std::tgamma(value + 1.0);
    if (!std::isfinite(result)) {
        throw std::runtime_error(
            "math.factorial is undefined at negative integers or outside the number range");
    }
    return result;
}

extern "C" double sun_llvm_math_acos(double value) { return std::acos(value); }
extern "C" double sun_llvm_math_asin(double value) { return std::asin(value); }
extern "C" double sun_llvm_math_atan(double value) { return std::atan(value); }
extern "C" double sun_llvm_math_atan2(double left, double right) {
    return std::atan2(left, right);
}
extern "C" double sun_llvm_math_cos(double value) { return std::cos(value); }
extern "C" double sun_llvm_math_exp(double value) { return std::exp(value); }
extern "C" double sun_llvm_math_fmod(double left, double right) {
    return std::fmod(left, right);
}
extern "C" double sun_llvm_math_hypot(double left, double right) {
    return std::hypot(left, right);
}
extern "C" double sun_llvm_math_log(double value) { return std::log(value); }
extern "C" double sun_llvm_math_log10(double value) { return std::log10(value); }
extern "C" double sun_llvm_math_log2(double value) { return std::log2(value); }
extern "C" double sun_llvm_math_pow(double left, double right) {
    return std::pow(left, right);
}
extern "C" double sun_llvm_math_sin(double value) { return std::sin(value); }
extern "C" double sun_llvm_math_sqrt(double value) { return std::sqrt(value); }
extern "C" double sun_llvm_math_tan(double value) { return std::tan(value); }

namespace {

std::string sun_llvm_read_line(const char* prompt, std::int64_t prompt_length) {
    std::cout.write(prompt, static_cast<std::streamsize>(prompt_length));
    std::cout.flush();
    std::string value;
    if (!std::getline(std::cin, value)) {
        throw std::runtime_error("input could not read a line");
    }
    return value;
}

bool sun_llvm_is_one_utf8_character(const std::string& value) {
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

} // namespace

extern "C" const char* sun_llvm_input_string(const char* prompt,
                                              std::int64_t prompt_length,
                                              std::int64_t* result_length) {
    return sun_llvm_store_string(sun_llvm_read_line(prompt, prompt_length),
                                 result_length);
}

extern "C" double sun_llvm_input_number(const char* prompt,
                                          std::int64_t prompt_length) {
    const auto text = sun_llvm_read_line(prompt, prompt_length);
    std::istringstream input(text);
    double value = 0.0;
    if (!(input >> value)) throw std::runtime_error("input expected a number");
    input >> std::ws;
    if (!input.eof()) throw std::runtime_error("input expected a number");
    return value;
}

extern "C" std::int64_t sun_llvm_input_int(const char* prompt,
                                             std::int64_t prompt_length) {
    const auto text = sun_llvm_read_line(prompt, prompt_length);
    std::istringstream input(text);
    std::int64_t value = 0;
    if (!(input >> value)) throw std::runtime_error("input expected an int");
    input >> std::ws;
    if (!input.eof()) throw std::runtime_error("input expected an int");
    return value;
}

extern "C" bool sun_llvm_input_bool(const char* prompt,
                                     std::int64_t prompt_length) {
    const auto value = sun_llvm_read_line(prompt, prompt_length);
    if (value == "true") return true;
    if (value == "false") return false;
    throw std::runtime_error("input expected 'true' or 'false'");
}

namespace {

struct SunLLVMFile {
    std::string path;
    std::string mode;
    std::fstream stream;
};

std::unordered_map<std::int64_t, std::unique_ptr<SunLLVMFile>> sun_llvm_files;
std::int64_t sun_llvm_next_file_handle = 1;

SunLLVMFile& sun_llvm_file(std::int64_t handle) {
    const auto found = sun_llvm_files.find(handle);
    if (found == sun_llvm_files.end()) {
        throw std::runtime_error("io file handle is closed or invalid");
    }
    return *found->second;
}

} // namespace

extern "C" std::int64_t sun_llvm_io_open(const char* path,
                                           std::int64_t path_length,
                                           const char* mode,
                                           std::int64_t mode_length) {
    auto file = std::make_unique<SunLLVMFile>();
    file->path.assign(path, static_cast<std::size_t>(path_length));
    file->mode.assign(mode, static_cast<std::size_t>(mode_length));
    auto flags = std::ios::openmode{};
    if (file->mode == "r") flags = std::ios::in;
    else if (file->mode == "rb") flags = std::ios::in | std::ios::binary;
    else if (file->mode == "w") flags = std::ios::out | std::ios::trunc;
    else if (file->mode == "a") flags = std::ios::out | std::ios::app;
    else if (file->mode == "pw") flags = std::ios::in | std::ios::out | std::ios::binary;
    else throw std::runtime_error("io.open mode must be 'r', 'w', 'a', 'rb', or 'pw'");
    file->stream.open(file->path, flags);
    if (!file->stream) {
        throw std::runtime_error("io.open could not open file '" + file->path + "'");
    }
    if (sun_llvm_next_file_handle <= 0 ||
        sun_llvm_next_file_handle == std::numeric_limits<std::int64_t>::max()) {
        throw std::runtime_error("io.open ran out of file handles");
    }
    const auto handle = sun_llvm_next_file_handle++;
    sun_llvm_files.emplace(handle, std::move(file));
    return handle;
}

extern "C" void sun_llvm_io_write(std::int64_t handle, const char* contents,
                                    std::int64_t length) {
    auto& file = sun_llvm_file(handle);
    if (file.mode != "w" && file.mode != "a") {
        throw std::runtime_error("io.write requires a file opened in 'w' or 'a' mode");
    }
    file.stream.write(contents, static_cast<std::streamsize>(length));
    file.stream.flush();
    if (!file.stream) {
        throw std::runtime_error("io.write failed while writing file '" + file.path + "'");
    }
}

extern "C" const char* sun_llvm_io_read(std::int64_t handle,
                                          std::int64_t* result_length) {
    auto& file = sun_llvm_file(handle);
    if (file.mode != "r" && file.mode != "rb") {
        throw std::runtime_error("io.read requires a file opened in 'r' or 'rb' mode");
    }
    std::string result((std::istreambuf_iterator<char>(file.stream)),
                       std::istreambuf_iterator<char>());
    if (file.stream.bad()) {
        throw std::runtime_error("io.read failed while reading file '" + file.path + "'");
    }
    *result_length = static_cast<std::int64_t>(result.size());
    return sun_llvm_store_string(std::move(result), result_length);
}

extern "C" void sun_llvm_io_close(std::int64_t handle) {
    const auto found = sun_llvm_files.find(handle);
    if (found == sun_llvm_files.end()) {
        throw std::runtime_error("io file handle is closed or invalid");
    }
    const auto path = found->second->path;
    found->second->stream.close();
    if (found->second->stream.fail()) {
        sun_llvm_files.erase(found);
        throw std::runtime_error("io.close failed for file '" + path + "'");
    }
    sun_llvm_files.erase(found);
}

extern "C" void sun_llvm_io_close_noexcept(std::int64_t handle) noexcept {
    const auto found = sun_llvm_files.find(handle);
    if (found == sun_llvm_files.end()) return;
    found->second->stream.close();
    sun_llvm_files.erase(found);
}

extern "C" const char* sun_llvm_input_char(const char* prompt,
                                             std::int64_t prompt_length,
                                             std::int64_t* result_length) {
    auto value = sun_llvm_read_line(prompt, prompt_length);
    if (!sun_llvm_is_one_utf8_character(value)) {
        throw std::runtime_error("input expected one UTF-8 character");
    }
    return sun_llvm_store_string(std::move(value), result_length);
}

struct SunDynamicDict {
    std::map<std::string, std::string> values;
};

static std::string sun_dict_i64_key(std::int64_t key) {
    const auto ordered = static_cast<std::uint64_t>(key) ^ (std::uint64_t{1} << 63);
    auto digits = std::to_string(ordered);
    return "i:" + std::string(20 - digits.size(), '0') + digits;
}
static std::string sun_dict_string_key(const char* key, std::int64_t length) {
    return "s:" + std::string(key, static_cast<std::size_t>(length));
}
static std::string sun_dict_bool_key(bool key) { return key ? "b:1" : "b:0"; }
static std::string sun_dict_value_i64(std::int64_t value) { return "i:" + std::to_string(value); }
static std::string sun_dict_value_double(double value) { return "d:" + std::to_string(value); }
static std::string sun_dict_value_bool(bool value) { return value ? "b:1" : "b:0"; }
static std::string sun_dict_value_string(const char* value, std::int64_t length) {
    return "s:" + std::string(value, static_cast<std::size_t>(length));
}
static std::string sun_dict_value_ptr(void* value) {
    return "p:" + std::to_string(reinterpret_cast<std::uintptr_t>(value));
}
static std::int64_t sun_parse_dict_i64(const std::string& value);
static double sun_parse_dict_double(const std::string& value);
static bool sun_parse_dict_bool(const std::string& value);
static std::string sun_parse_dict_string(const std::string& value);
static void* sun_parse_dict_ptr(const std::string& value) {
    if (value.rfind("p:", 0) != 0) {
        throw std::runtime_error("dictionary value type mismatch");
    }
    return reinterpret_cast<void*>(
        static_cast<std::uintptr_t>(std::stoull(value.substr(2))));
}

extern "C" void* sun_llvm_dict_create() { return new SunDynamicDict{}; }
extern "C" std::int64_t sun_llvm_dict_len(void* dict) {
    return static_cast<std::int64_t>(static_cast<SunDynamicDict*>(dict)->values.size());
}
extern "C" const char* sun_llvm_dict_key_at(void* dict, double index,
                                              std::int64_t* length) {
    const auto& values = static_cast<SunDynamicDict*>(dict)->values;
    auto entry = values.begin();
    std::advance(entry, sun_list_index(values.size(), index));
    const auto& encoded = entry->first;
    std::string key;
    if (encoded.rfind("s:", 0) == 0) {
        key = encoded.substr(2);
    } else if (encoded.rfind("i:", 0) == 0) {
        const auto ordered = std::stoull(encoded.substr(2));
        key = std::to_string(static_cast<std::int64_t>(
            ordered ^ (std::uint64_t{1} << 63)));
    } else {
        key = encoded == "b:1" ? "true" : "false";
    }
    return sun_llvm_store_string(std::move(key), length);
}
extern "C" void* sun_llvm_dict_value_ptr_at(void* dict, double index) {
    const auto& values = static_cast<SunDynamicDict*>(dict)->values;
    auto entry = values.begin();
    std::advance(entry, sun_list_index(values.size(), index));
    return sun_parse_dict_ptr(entry->second);
}
extern "C" void sun_llvm_dict_clear(void* dict) {
    static_cast<SunDynamicDict*>(dict)->values.clear();
}
extern "C" void sun_llvm_dict_set_i64_i64(void* dict, std::int64_t key, std::int64_t value) {
    static_cast<SunDynamicDict*>(dict)->values[sun_dict_i64_key(key)] = sun_dict_value_i64(value);
}
extern "C" void sun_llvm_dict_set_i64_double(void* dict, std::int64_t key, double value) {
    static_cast<SunDynamicDict*>(dict)->values[sun_dict_i64_key(key)] = sun_dict_value_double(value);
}
extern "C" void sun_llvm_dict_set_i64_bool(void* dict, std::int64_t key, bool value) {
    static_cast<SunDynamicDict*>(dict)->values[sun_dict_i64_key(key)] = sun_dict_value_bool(value);
}
extern "C" void sun_llvm_dict_set_i64_string(void* dict, std::int64_t key, const char* value, std::int64_t length) {
    static_cast<SunDynamicDict*>(dict)->values[sun_dict_i64_key(key)] = sun_dict_value_string(value, length);
}
extern "C" void sun_llvm_dict_set_string_i64(void* dict, const char* key, std::int64_t key_length,
                                               std::int64_t value) {
    static_cast<SunDynamicDict*>(dict)->values[sun_dict_string_key(key, key_length)] = sun_dict_value_i64(value);
}
extern "C" void sun_llvm_dict_set_string_double(void* dict, const char* key, std::int64_t key_length,
                                                 double value) {
    static_cast<SunDynamicDict*>(dict)->values[sun_dict_string_key(key, key_length)] = sun_dict_value_double(value);
}
extern "C" void sun_llvm_dict_set_string_bool(void* dict, const char* key, std::int64_t key_length,
                                               bool value) {
    static_cast<SunDynamicDict*>(dict)->values[sun_dict_string_key(key, key_length)] = sun_dict_value_bool(value);
}
extern "C" void sun_llvm_dict_set_string_string(void* dict, const char* key, std::int64_t key_length,
                                                 const char* value, std::int64_t value_length) {
    static_cast<SunDynamicDict*>(dict)->values[sun_dict_string_key(key, key_length)] = sun_dict_value_string(value, value_length);
}
extern "C" void sun_llvm_dict_set_i64_ptr(void* dict, std::int64_t key, void* value) {
    static_cast<SunDynamicDict*>(dict)->values[sun_dict_i64_key(key)] =
        sun_dict_value_ptr(value);
}
extern "C" void sun_llvm_dict_set_string_ptr(void* dict, const char* key,
                                                std::int64_t key_length, void* value) {
    static_cast<SunDynamicDict*>(dict)->values[sun_dict_string_key(key, key_length)] =
        sun_dict_value_ptr(value);
}
extern "C" void sun_llvm_dict_set_bool_ptr(void* dict, bool key, void* value) {
    static_cast<SunDynamicDict*>(dict)->values[sun_dict_bool_key(key)] =
        sun_dict_value_ptr(value);
}
extern "C" void sun_llvm_dict_set_bool_i64(void* dict, bool key, std::int64_t value) {
    static_cast<SunDynamicDict*>(dict)->values[sun_dict_bool_key(key)] = sun_dict_value_i64(value);
}
extern "C" void sun_llvm_dict_set_bool_double(void* dict, bool key, double value) {
    static_cast<SunDynamicDict*>(dict)->values[sun_dict_bool_key(key)] = sun_dict_value_double(value);
}
extern "C" void sun_llvm_dict_set_bool_bool(void* dict, bool key, bool value) {
    static_cast<SunDynamicDict*>(dict)->values[sun_dict_bool_key(key)] = sun_dict_value_bool(value);
}
extern "C" void sun_llvm_dict_set_bool_string(void* dict, bool key, const char* value,
                                               std::int64_t length) {
    static_cast<SunDynamicDict*>(dict)->values[sun_dict_bool_key(key)] = sun_dict_value_string(value, length);
}

extern "C" std::int64_t sun_llvm_dict_at_i64_i64(void* dict, std::int64_t key) {
    auto& values = static_cast<SunDynamicDict*>(dict)->values;
    const auto found = values.find(sun_dict_i64_key(key));
    if (found == values.end() || found->second.rfind("i:", 0) != 0) throw std::out_of_range("dictionary key not found");
    return std::stoll(found->second.substr(2));
}
extern "C" double sun_llvm_dict_at_i64_double(void* dict, std::int64_t key) {
    auto& values = static_cast<SunDynamicDict*>(dict)->values;
    const auto found = values.find(sun_dict_i64_key(key));
    if (found == values.end() || found->second.rfind("d:", 0) != 0) throw std::out_of_range("dictionary key not found");
    return std::stod(found->second.substr(2));
}
extern "C" bool sun_llvm_dict_at_i64_bool(void* dict, std::int64_t key) {
    auto& values = static_cast<SunDynamicDict*>(dict)->values;
    const auto found = values.find(sun_dict_i64_key(key));
    if (found == values.end() || found->second.rfind("b:", 0) != 0) throw std::out_of_range("dictionary key not found");
    return found->second[2] == '1';
}
extern "C" const char* sun_llvm_dict_at_i64_string(void* dict, std::int64_t key, std::int64_t* length) {
    auto& values = static_cast<SunDynamicDict*>(dict)->values;
    const auto found = values.find(sun_dict_i64_key(key));
    if (found == values.end() || found->second.rfind("s:", 0) != 0) throw std::out_of_range("dictionary key not found");
    auto* stored = new std::string(found->second.substr(2));
    *length = static_cast<std::int64_t>(stored->size());
    return stored->data();
}
extern "C" std::int64_t sun_llvm_dict_at_string_i64(void* dict, const char* key, std::int64_t key_length) {
    auto& values = static_cast<SunDynamicDict*>(dict)->values;
    const auto found = values.find(sun_dict_string_key(key, key_length));
    if (found == values.end() || found->second.rfind("i:", 0) != 0) throw std::out_of_range("dictionary key not found");
    return std::stoll(found->second.substr(2));
}
extern "C" double sun_llvm_dict_at_string_double(void* dict, const char* key, std::int64_t key_length) {
    auto& values = static_cast<SunDynamicDict*>(dict)->values;
    const auto found = values.find(sun_dict_string_key(key, key_length));
    if (found == values.end() || found->second.rfind("d:", 0) != 0) throw std::out_of_range("dictionary key not found");
    return std::stod(found->second.substr(2));
}
extern "C" bool sun_llvm_dict_at_string_bool(void* dict, const char* key, std::int64_t key_length) {
    auto& values = static_cast<SunDynamicDict*>(dict)->values;
    const auto found = values.find(sun_dict_string_key(key, key_length));
    if (found == values.end() || found->second.rfind("b:", 0) != 0) throw std::out_of_range("dictionary key not found");
    return found->second[2] == '1';
}
extern "C" const char* sun_llvm_dict_at_string_string(void* dict, const char* key, std::int64_t key_length,
                                                      std::int64_t* length) {
    auto& values = static_cast<SunDynamicDict*>(dict)->values;
    const auto found = values.find(sun_dict_string_key(key, key_length));
    if (found == values.end() || found->second.rfind("s:", 0) != 0) throw std::out_of_range("dictionary key not found");
    auto* stored = new std::string(found->second.substr(2));
    *length = static_cast<std::int64_t>(stored->size());
    return stored->data();
}
extern "C" void* sun_llvm_dict_at_i64_ptr(void* dict, std::int64_t key) {
    const auto& values = static_cast<SunDynamicDict*>(dict)->values;
    const auto found = values.find(sun_dict_i64_key(key));
    if (found == values.end()) throw std::out_of_range("dictionary key not found");
    return sun_parse_dict_ptr(found->second);
}
extern "C" void* sun_llvm_dict_at_string_ptr(void* dict, const char* key,
                                                std::int64_t key_length) {
    const auto& values = static_cast<SunDynamicDict*>(dict)->values;
    const auto found = values.find(sun_dict_string_key(key, key_length));
    if (found == values.end()) throw std::out_of_range("dictionary key not found");
    return sun_parse_dict_ptr(found->second);
}
extern "C" void* sun_llvm_dict_at_bool_ptr(void* dict, bool key) {
    const auto& values = static_cast<SunDynamicDict*>(dict)->values;
    const auto found = values.find(sun_dict_bool_key(key));
    if (found == values.end()) throw std::out_of_range("dictionary key not found");
    return sun_parse_dict_ptr(found->second);
}
extern "C" std::int64_t sun_llvm_dict_at_bool_i64(void* dict, bool key) {
    return sun_parse_dict_i64(static_cast<SunDynamicDict*>(dict)->values.at(sun_dict_bool_key(key)));
}
extern "C" double sun_llvm_dict_at_bool_double(void* dict, bool key) {
    return sun_parse_dict_double(static_cast<SunDynamicDict*>(dict)->values.at(sun_dict_bool_key(key)));
}
extern "C" bool sun_llvm_dict_at_bool_bool(void* dict, bool key) {
    return sun_parse_dict_bool(static_cast<SunDynamicDict*>(dict)->values.at(sun_dict_bool_key(key)));
}
extern "C" const char* sun_llvm_dict_at_bool_string(void* dict, bool key, std::int64_t* length) {
    auto* stored = new std::string(sun_parse_dict_string(
        static_cast<SunDynamicDict*>(dict)->values.at(sun_dict_bool_key(key))));
    *length = static_cast<std::int64_t>(stored->size());
    return stored->data();
}
extern "C" bool sun_llvm_dict_contains_i64(void* dict, std::int64_t key) {
    auto& values = static_cast<SunDynamicDict*>(dict)->values;
    return values.find(sun_dict_i64_key(key)) != values.end();
}
extern "C" bool sun_llvm_dict_contains_string(void* dict, const char* key, std::int64_t key_length) {
    auto& values = static_cast<SunDynamicDict*>(dict)->values;
    return values.find(sun_dict_string_key(key, key_length)) != values.end();
}
extern "C" bool sun_llvm_dict_remove_i64(void* dict, std::int64_t key) {
    auto& values = static_cast<SunDynamicDict*>(dict)->values;
    return values.erase(sun_dict_i64_key(key)) != 0;
}
extern "C" bool sun_llvm_dict_remove_string(void* dict, const char* key, std::int64_t key_length) {
    auto& values = static_cast<SunDynamicDict*>(dict)->values;
    return values.erase(sun_dict_string_key(key, key_length)) != 0;
}
extern "C" bool sun_llvm_dict_contains_bool(void* dict, bool key) {
    return static_cast<SunDynamicDict*>(dict)->values.find(sun_dict_bool_key(key)) !=
           static_cast<SunDynamicDict*>(dict)->values.end();
}
extern "C" bool sun_llvm_dict_remove_bool(void* dict, bool key) {
    return static_cast<SunDynamicDict*>(dict)->values.erase(sun_dict_bool_key(key)) != 0;
}

template <typename Result, typename Parse>
static Result sun_dict_get_value(void* dict, const std::string& key,
                                Result fallback, Parse parse) {
    auto& values = static_cast<SunDynamicDict*>(dict)->values;
    const auto found = values.find(key);
    return found == values.end() ? fallback : parse(found->second);
}

static std::int64_t sun_parse_dict_i64(const std::string& value) {
    if (value.rfind("i:", 0) != 0) throw std::out_of_range("dictionary value type mismatch");
    return std::stoll(value.substr(2));
}
static double sun_parse_dict_double(const std::string& value) {
    if (value.rfind("d:", 0) != 0) throw std::out_of_range("dictionary value type mismatch");
    return std::stod(value.substr(2));
}
static bool sun_parse_dict_bool(const std::string& value) {
    if (value.rfind("b:", 0) != 0) throw std::out_of_range("dictionary value type mismatch");
    return value[2] == '1';
}
static std::string sun_parse_dict_string(const std::string& value) {
    if (value.rfind("s:", 0) != 0) throw std::out_of_range("dictionary value type mismatch");
    return value.substr(2);
}

extern "C" std::int64_t sun_llvm_dict_get_i64_i64(void* dict, std::int64_t key,
                                                    std::int64_t fallback) {
    return sun_dict_get_value<std::int64_t>(dict, sun_dict_i64_key(key), fallback,
                                            sun_parse_dict_i64);
}
extern "C" double sun_llvm_dict_get_i64_double(void* dict, std::int64_t key,
                                                double fallback) {
    return sun_dict_get_value<double>(dict, sun_dict_i64_key(key), fallback,
                                      sun_parse_dict_double);
}
extern "C" bool sun_llvm_dict_get_i64_bool(void* dict, std::int64_t key, bool fallback) {
    return sun_dict_get_value<bool>(dict, sun_dict_i64_key(key), fallback,
                                    sun_parse_dict_bool);
}
extern "C" std::int64_t sun_llvm_dict_get_string_i64(void* dict, const char* key,
                                                      std::int64_t key_length,
                                                      std::int64_t fallback) {
    return sun_dict_get_value<std::int64_t>(dict, sun_dict_string_key(key, key_length),
                                            fallback, sun_parse_dict_i64);
}
extern "C" double sun_llvm_dict_get_string_double(void* dict, const char* key,
                                                    std::int64_t key_length,
                                                    double fallback) {
    return sun_dict_get_value<double>(dict, sun_dict_string_key(key, key_length),
                                      fallback, sun_parse_dict_double);
}
extern "C" bool sun_llvm_dict_get_string_bool(void* dict, const char* key,
                                               std::int64_t key_length, bool fallback) {
    return sun_dict_get_value<bool>(dict, sun_dict_string_key(key, key_length),
                                    fallback, sun_parse_dict_bool);
}
extern "C" const char* sun_llvm_dict_get_i64_string(void* dict, std::int64_t key,
                                                     const char* fallback,
                                                     std::int64_t fallback_length,
                                                     std::int64_t* result_length) {
    auto& values = static_cast<SunDynamicDict*>(dict)->values;
    const auto found = values.find(sun_dict_i64_key(key));
    auto* result = new std::string(found == values.end()
                                       ? std::string(fallback, static_cast<std::size_t>(fallback_length))
                                       : sun_parse_dict_string(found->second));
    *result_length = static_cast<std::int64_t>(result->size());
    return result->data();
}
extern "C" const char* sun_llvm_dict_get_string_string(void* dict, const char* key,
                                                        std::int64_t key_length,
                                                        const char* fallback,
                                                        std::int64_t fallback_length,
                                                        std::int64_t* result_length) {
    auto& values = static_cast<SunDynamicDict*>(dict)->values;
    const auto found = values.find(sun_dict_string_key(key, key_length));
    auto* result = new std::string(found == values.end()
                                       ? std::string(fallback, static_cast<std::size_t>(fallback_length))
                                       : sun_parse_dict_string(found->second));
    *result_length = static_cast<std::int64_t>(result->size());
    return result->data();
}
extern "C" std::int64_t sun_llvm_dict_get_bool_i64(void* dict, bool key, std::int64_t fallback) {
    return sun_dict_get_value<std::int64_t>(dict, sun_dict_bool_key(key), fallback, sun_parse_dict_i64);
}
extern "C" void* sun_llvm_dict_get_i64_ptr(void* dict, std::int64_t key,
                                              void* fallback) {
    const auto& values = static_cast<SunDynamicDict*>(dict)->values;
    const auto found = values.find(sun_dict_i64_key(key));
    return found == values.end() ? fallback : sun_parse_dict_ptr(found->second);
}
extern "C" void* sun_llvm_dict_get_string_ptr(void* dict, const char* key,
                                                 std::int64_t key_length,
                                                 void* fallback) {
    const auto& values = static_cast<SunDynamicDict*>(dict)->values;
    const auto found = values.find(sun_dict_string_key(key, key_length));
    return found == values.end() ? fallback : sun_parse_dict_ptr(found->second);
}
extern "C" void* sun_llvm_dict_get_bool_ptr(void* dict, bool key, void* fallback) {
    const auto& values = static_cast<SunDynamicDict*>(dict)->values;
    const auto found = values.find(sun_dict_bool_key(key));
    return found == values.end() ? fallback : sun_parse_dict_ptr(found->second);
}
extern "C" double sun_llvm_dict_get_bool_double(void* dict, bool key, double fallback) {
    return sun_dict_get_value<double>(dict, sun_dict_bool_key(key), fallback, sun_parse_dict_double);
}
extern "C" bool sun_llvm_dict_get_bool_bool(void* dict, bool key, bool fallback) {
    return sun_dict_get_value<bool>(dict, sun_dict_bool_key(key), fallback, sun_parse_dict_bool);
}
extern "C" const char* sun_llvm_dict_get_bool_string(void* dict, bool key,
                                                      const char* fallback,
                                                      std::int64_t fallback_length,
                                                      std::int64_t* result_length) {
    auto& values = static_cast<SunDynamicDict*>(dict)->values;
    const auto found = values.find(sun_dict_bool_key(key));
    auto* result = new std::string(found == values.end()
        ? std::string(fallback, static_cast<std::size_t>(fallback_length))
        : sun_parse_dict_string(found->second));
    *result_length = static_cast<std::int64_t>(result->size());
    return result->data();
}

extern "C" void* sun_llvm_dict_keys_i64(void* dict) {
    auto* result = new SunIntList{};
    for (const auto& entry : static_cast<SunDynamicDict*>(dict)->values) {
        if (entry.first.rfind("i:", 0) == 0) {
            const auto ordered = std::stoull(entry.first.substr(2));
            result->values.push_back(static_cast<std::int64_t>(ordered ^ (std::uint64_t{1} << 63)));
        }
    }
    return result;
}
extern "C" void* sun_llvm_dict_keys_string(void* dict) {
    auto* result = new SunStringList{};
    for (const auto& entry : static_cast<SunDynamicDict*>(dict)->values) {
        if (entry.first.rfind("s:", 0) == 0) result->values.push_back(entry.first.substr(2));
    }
    return result;
}
extern "C" void* sun_llvm_dict_keys_bool(void* dict) {
    auto* result = new SunBoolList{};
    for (const auto& entry : static_cast<SunDynamicDict*>(dict)->values) {
        if (entry.first == "b:0") result->values.push_back(false);
        else if (entry.first == "b:1") result->values.push_back(true);
    }
    return result;
}
extern "C" const char* sun_llvm_dict_to_string(void* dict, std::int64_t* length) {
    std::ostringstream output;
    output << '{';
    bool first = true;
    for (const auto& entry : static_cast<SunDynamicDict*>(dict)->values) {
        if (!first) output << ", ";
        first = false;
        const auto& key = entry.first;
        if (key.rfind("i:", 0) == 0) {
            const auto ordered = std::stoull(key.substr(2));
            output << static_cast<std::int64_t>(ordered ^ (std::uint64_t{1} << 63));
        } else if (key.rfind("s:", 0) == 0) {
            output << key.substr(2);
        } else if (key == "b:1") {
            output << "true";
        } else {
            output << "false";
        }
        output << ": ";
        const auto& value = entry.second;
        if (value.rfind("i:", 0) == 0) output << std::stoll(value.substr(2));
        else if (value.rfind("d:", 0) == 0) output << std::stod(value.substr(2));
        else if (value.rfind("b:", 0) == 0) output << (value[2] == '1' ? "true" : "false");
        else output << value.substr(2);
    }
    output << '}';
    auto* stored = new std::string(output.str());
    *length = static_cast<std::int64_t>(stored->size());
    return stored->data();
}
extern "C" void* sun_llvm_dict_values_i64(void* dict) {
    auto* result = new SunIntList{};
    for (const auto& entry : static_cast<SunDynamicDict*>(dict)->values) result->values.push_back(sun_parse_dict_i64(entry.second));
    return result;
}
extern "C" void* sun_llvm_dict_values_double(void* dict) {
    auto* result = new SunNumberList{};
    for (const auto& entry : static_cast<SunDynamicDict*>(dict)->values) result->values.push_back(sun_parse_dict_double(entry.second));
    return result;
}
extern "C" void* sun_llvm_dict_values_bool(void* dict) {
    auto* result = new SunBoolList{};
    for (const auto& entry : static_cast<SunDynamicDict*>(dict)->values) result->values.push_back(sun_parse_dict_bool(entry.second));
    return result;
}
extern "C" void* sun_llvm_dict_values_string(void* dict) {
    auto* result = new SunStringList{};
    for (const auto& entry : static_cast<SunDynamicDict*>(dict)->values) result->values.push_back(sun_parse_dict_string(entry.second));
    return result;
}
extern "C" void* sun_llvm_dict_values_ptr(void* dict) {
    auto* result = new SunPointerList{};
    for (const auto& entry : static_cast<SunDynamicDict*>(dict)->values) {
        result->values.push_back(sun_parse_dict_ptr(entry.second));
    }
    return result;
}

extern "C" const char* sun_llvm_list_pop_string(void* list, double index, std::int64_t* length) {
    auto& values = static_cast<SunStringList*>(list)->values;
    auto pos = sun_list_index(values.size(), index);
    auto value = std::move(values[pos]);
    values.erase(values.begin() + pos);
    auto* stored = new std::string(std::move(value));
    *length = static_cast<std::int64_t>(stored->size());
    return stored->data();
}
