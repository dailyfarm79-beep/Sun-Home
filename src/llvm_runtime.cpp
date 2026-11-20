#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

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

} // namespace

extern "C" void* sun_llvm_list_create_i64() { return new SunIntList{}; }
extern "C" void* sun_llvm_list_create_double() { return new SunNumberList{}; }
extern "C" void* sun_llvm_list_create_bool() { return new SunBoolList{}; }
extern "C" void* sun_llvm_list_create_string() { return new SunStringList{}; }

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

extern "C" void sun_llvm_list_clear_i64(void* list) { static_cast<SunIntList*>(list)->values.clear(); }
extern "C" void sun_llvm_list_clear_double(void* list) { static_cast<SunNumberList*>(list)->values.clear(); }
extern "C" void sun_llvm_list_clear_bool(void* list) { static_cast<SunBoolList*>(list)->values.clear(); }
extern "C" void sun_llvm_list_clear_string(void* list) { static_cast<SunStringList*>(list)->values.clear(); }

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
static std::int64_t sun_parse_dict_i64(const std::string& value);
static double sun_parse_dict_double(const std::string& value);
static bool sun_parse_dict_bool(const std::string& value);
static std::string sun_parse_dict_string(const std::string& value);

extern "C" void* sun_llvm_dict_create() { return new SunDynamicDict{}; }
extern "C" std::int64_t sun_llvm_dict_len(void* dict) {
    return static_cast<std::int64_t>(static_cast<SunDynamicDict*>(dict)->values.size());
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

extern "C" const char* sun_llvm_list_pop_string(void* list, double index, std::int64_t* length) {
    auto& values = static_cast<SunStringList*>(list)->values;
    auto pos = sun_list_index(values.size(), index);
    auto value = std::move(values[pos]);
    values.erase(values.begin() + pos);
    auto* stored = new std::string(std::move(value));
    *length = static_cast<std::int64_t>(stored->size());
    return stored->data();
}
