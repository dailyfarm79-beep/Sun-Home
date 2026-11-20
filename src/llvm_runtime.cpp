#include <cmath>
#include <cstdint>
#include <cstring>
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

extern "C" const char* sun_llvm_list_pop_string(void* list, double index, std::int64_t* length) {
    auto& values = static_cast<SunStringList*>(list)->values;
    auto pos = sun_list_index(values.size(), index);
    auto value = std::move(values[pos]);
    values.erase(values.begin() + pos);
    auto* stored = new std::string(std::move(value));
    *length = static_cast<std::int64_t>(stored->size());
    return stored->data();
}
