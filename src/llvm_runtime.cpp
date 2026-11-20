#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
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
    return "i:" + std::to_string(key);
}
static std::string sun_dict_string_key(const char* key, std::int64_t length) {
    const std::string value(key, static_cast<std::size_t>(length));
    return "s:" + std::to_string(length) + ':' + value;
}
static std::string sun_dict_value_i64(std::int64_t value) { return "i:" + std::to_string(value); }
static std::string sun_dict_value_double(double value) { return "d:" + std::to_string(value); }
static std::string sun_dict_value_bool(bool value) { return value ? "b:1" : "b:0"; }
static std::string sun_dict_value_string(const char* value, std::int64_t length) {
    return "s:" + std::string(value, static_cast<std::size_t>(length));
}

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

extern "C" const char* sun_llvm_list_pop_string(void* list, double index, std::int64_t* length) {
    auto& values = static_cast<SunStringList*>(list)->values;
    auto pos = sun_list_index(values.size(), index);
    auto value = std::move(values[pos]);
    values.erase(values.begin() + pos);
    auto* stored = new std::string(std::move(value));
    *length = static_cast<std::int64_t>(stored->size());
    return stored->data();
}
