#include "stdlib_json.hpp"

namespace sun {

std::string json_runtime_cpp() {
    return R"cpp(
class SunJson {
public:
    using Array = std::vector<SunJson>;
    using Object = std::map<std::string, SunJson>;
    struct ParsedNumber {
        std::string literal;
        double approximation;

        friend bool operator==(const ParsedNumber& left, const ParsedNumber& right) {
            return left.approximation == right.approximation;
        }
    };
    using Value = std::variant<std::nullptr_t, bool, std::int64_t, double,
                               ParsedNumber, std::string, Array, Object>;

    SunJson() : value_(nullptr) {}
    explicit SunJson(Value value) : value_(std::move(value)) {}

    static SunJson parse(const std::string& text) {
        class Parser {
        public:
            explicit Parser(const std::string& source) : source_(source) {}

            SunJson parse() {
                skip_space();
                auto result = parse_value();
                skip_space();
                if (position_ != source_.size()) fail("unexpected trailing data");
                return result;
            }

        private:
            [[noreturn]] void fail(const std::string& message) const {
                throw SunRuntimeError("json.loads " + message +
                                      " at byte " + std::to_string(position_));
            }

            void skip_space() {
                while (position_ < source_.size() &&
                       (source_[position_] == ' ' || source_[position_] == '\t' ||
                        source_[position_] == '\r' || source_[position_] == '\n')) {
                    ++position_;
                }
            }

            bool consume(char expected) {
                if (position_ < source_.size() && source_[position_] == expected) {
                    ++position_;
                    return true;
                }
                return false;
            }

            bool consume_literal(const char* literal) {
                const std::string expected(literal);
                if (source_.compare(position_, expected.size(), expected) != 0) return false;
                position_ += expected.size();
                return true;
            }

            SunJson parse_value() {
                if (position_ == source_.size()) fail("expected a value");
                const char current = source_[position_];
                if (current == '"') return SunJson(SunJson::Value{parse_string()});
                if (current == '{') return parse_object();
                if (current == '[') return parse_array();
                if (current == 't' && consume_literal("true"))
                    return SunJson(SunJson::Value{true});
                if (current == 'f' && consume_literal("false"))
                    return SunJson(SunJson::Value{false});
                if (current == 'n' && consume_literal("null"))
                    return SunJson(SunJson::Value{nullptr});
                if (current == '-' || (current >= '0' && current <= '9'))
                    return parse_number();
                fail("expected a value");
            }

            static void append_utf8(std::string& output, std::uint32_t codepoint) {
                if (codepoint <= 0x7f) {
                    output.push_back(static_cast<char>(codepoint));
                } else if (codepoint <= 0x7ff) {
                    output.push_back(static_cast<char>(0xc0 | (codepoint >> 6)));
                    output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
                } else if (codepoint <= 0xffff) {
                    output.push_back(static_cast<char>(0xe0 | (codepoint >> 12)));
                    output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
                    output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
                } else {
                    output.push_back(static_cast<char>(0xf0 | (codepoint >> 18)));
                    output.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f)));
                    output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
                    output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
                }
            }

            unsigned parse_hex_quad() {
                if (source_.size() - position_ < 4) fail("incomplete unicode escape");
                unsigned value = 0;
                for (int i = 0; i < 4; ++i) {
                    const char c = source_[position_++];
                    value <<= 4;
                    if (c >= '0' && c <= '9') value |= static_cast<unsigned>(c - '0');
                    else if (c >= 'a' && c <= 'f') value |= static_cast<unsigned>(c - 'a' + 10);
                    else if (c >= 'A' && c <= 'F') value |= static_cast<unsigned>(c - 'A' + 10);
                    else fail("invalid unicode escape");
                }
                return value;
            }

            std::string parse_string() {
                if (!consume('"')) fail("expected a string");
                std::string result;
                while (position_ < source_.size()) {
                    const auto byte = static_cast<unsigned char>(source_[position_++]);
                    if (byte == '"') return result;
                    if (byte < 0x20) fail("unescaped control character in string");
                    if (byte != '\\') {
                        result.push_back(static_cast<char>(byte));
                        continue;
                    }
                    if (position_ == source_.size()) fail("incomplete string escape");
                    const char escaped = source_[position_++];
                    switch (escaped) {
                    case '"': result.push_back('"'); break;
                    case '\\': result.push_back('\\'); break;
                    case '/': result.push_back('/'); break;
                    case 'b': result.push_back('\b'); break;
                    case 'f': result.push_back('\f'); break;
                    case 'n': result.push_back('\n'); break;
                    case 'r': result.push_back('\r'); break;
                    case 't': result.push_back('\t'); break;
                    case 'u': {
                        auto point = parse_hex_quad();
                        if (point >= 0xd800 && point <= 0xdbff) {
                            if (source_.size() - position_ < 2 ||
                                source_[position_] != '\\' ||
                                source_[position_ + 1] != 'u') {
                                fail("high surrogate without low surrogate");
                            }
                            position_ += 2;
                            const auto low = parse_hex_quad();
                            if (low < 0xdc00 || low > 0xdfff)
                                fail("invalid low surrogate");
                            point = 0x10000 + ((point - 0xd800) << 10) + (low - 0xdc00);
                        } else if (point >= 0xdc00 && point <= 0xdfff) {
                            fail("unexpected low surrogate");
                        }
                        append_utf8(result, point);
                        break;
                    }
                    default: fail("invalid string escape");
                    }
                }
                fail("unterminated string");
            }

            SunJson parse_number() {
                const auto start = position_;
                consume('-');
                if (consume('0')) {
                    if (position_ < source_.size() &&
                        source_[position_] >= '0' && source_[position_] <= '9') {
                        fail("leading zero in number");
                    }
                } else {
                    const auto digits = position_;
                    while (position_ < source_.size() &&
                           source_[position_] >= '0' && source_[position_] <= '9') ++position_;
                    if (digits == position_) fail("expected digits in number");
                }
                if (consume('.')) {
                    const auto digits = position_;
                    while (position_ < source_.size() &&
                           source_[position_] >= '0' && source_[position_] <= '9') ++position_;
                    if (digits == position_) fail("expected digits after decimal point");
                }
                if (consume('e') || consume('E')) {
                    if (!consume('+')) consume('-');
                    const auto digits = position_;
                    while (position_ < source_.size() &&
                           source_[position_] >= '0' && source_[position_] <= '9') ++position_;
                    if (digits == position_) fail("expected digits in exponent");
                }
                const auto token = source_.substr(start, position_ - start);
                if (token.find_first_of(".eE") == std::string::npos) {
                    std::int64_t integer = 0;
                    const auto conversion = std::from_chars(
                        token.data(), token.data() + token.size(), integer);
                    if (conversion.ec == std::errc{} &&
                        conversion.ptr == token.data() + token.size()) {
                        return SunJson(SunJson::Value{integer});
                    }
                    if (conversion.ec != std::errc::result_out_of_range ||
                        conversion.ptr != token.data() + token.size()) {
                        fail("invalid number");
                    }
                }
                double number = 0.0;
                std::size_t parsed = 0;
                try {
                    number = std::stod(token, &parsed);
                } catch (const std::invalid_argument&) {
                    fail("invalid number");
                } catch (const std::out_of_range&) {
                    fail("number is out of range");
                }
                if (parsed != token.size() || !std::isfinite(number))
                    fail("number is out of range");
                return SunJson(SunJson::Value{ParsedNumber{token, number}});
            }

            SunJson parse_array() {
                consume('[');
                skip_space();
                Array items;
                if (consume(']')) return SunJson(SunJson::Value{std::move(items)});
                while (true) {
                    skip_space();
                    items.push_back(parse_value());
                    skip_space();
                    if (consume(']')) break;
                    if (!consume(',')) fail("expected ',' or ']' in array");
                    skip_space();
                }
                return SunJson(SunJson::Value{std::move(items)});
            }

            SunJson parse_object() {
                consume('{');
                skip_space();
                Object fields;
                if (consume('}')) return SunJson(SunJson::Value{std::move(fields)});
                while (true) {
                    skip_space();
                    if (position_ == source_.size() || source_[position_] != '"')
                        fail("object keys must be strings");
                    auto key = parse_string();
                    skip_space();
                    if (!consume(':')) fail("expected ':' after object key");
                    skip_space();
                    auto item = parse_value();
                    fields[std::move(key)] = std::move(item);
                    skip_space();
                    if (consume('}')) break;
                    if (!consume(',')) fail("expected ',' or '}' in object");
                    skip_space();
                }
                return SunJson(SunJson::Value{std::move(fields)});
            }

            const std::string& source_;
            std::size_t position_ = 0;
        };
        return Parser(text).parse();
    }

    std::string dump() const {
        if (std::holds_alternative<std::nullptr_t>(value_)) return "null";
        if (const auto* boolean = std::get_if<bool>(&value_))
            return *boolean ? "true" : "false";
        if (const auto* integer = std::get_if<std::int64_t>(&value_))
            return std::to_string(*integer);
        if (const auto* integer = std::get_if<ParsedNumber>(&value_))
            return integer->literal;
        if (const auto* number = std::get_if<double>(&value_)) {
            std::ostringstream result;
            result << std::setprecision(std::numeric_limits<double>::max_digits10) << *number;
            return result.str();
        }
        if (const auto* string = std::get_if<std::string>(&value_))
            return dump_string(*string);
        if (const auto* array = std::get_if<Array>(&value_)) {
            std::string result = "[";
            for (std::size_t i = 0; i < array->size(); ++i) {
                if (i) result += ',';
                result += (*array)[i].dump();
            }
            return result + ']';
        }
        std::string result = "{";
        bool first = true;
        for (const auto& entry : std::get<Object>(value_)) {
            if (!first) result += ',';
            first = false;
            result += dump_string(entry.first) + ':' + entry.second.dump();
        }
        return result + '}';
    }

    bool is_null() const { return std::holds_alternative<std::nullptr_t>(value_); }
    double as_number() const {
        if (const auto* integer = std::get_if<std::int64_t>(&value_))
            return static_cast<double>(*integer);
        if (const auto* integer = std::get_if<ParsedNumber>(&value_))
            return integer->approximation;
        const auto* result = std::get_if<double>(&value_);
        if (!result) throw SunRuntimeError("json value is not a number");
        return *result;
    }
    std::int64_t as_int() const {
        if (const auto* integer = std::get_if<std::int64_t>(&value_)) return *integer;
        if (const auto* parsed = std::get_if<ParsedNumber>(&value_)) {
            std::int64_t integer = 0;
            if (parse_int64(parsed->literal, integer)) return integer;
            throw SunRuntimeError("json value is not an in-range integer");
        }
        const auto* number = std::get_if<double>(&value_);
        const double limit = std::ldexp(1.0, 63);
        if (!number || !std::isfinite(*number) || std::floor(*number) != *number ||
            *number < -limit || *number >= limit) {
            throw SunRuntimeError("json value is not an in-range integer");
        }
        return static_cast<std::int64_t>(*number);
    }
    bool as_bool() const {
        const auto* result = std::get_if<bool>(&value_);
        if (!result) throw SunRuntimeError("json value is not a bool");
        return *result;
    }
    std::string as_string() const {
        const auto* result = std::get_if<std::string>(&value_);
        if (!result) throw SunRuntimeError("json value is not a string");
        return *result;
    }
    std::int64_t size() const {
        if (const auto* array = std::get_if<Array>(&value_))
            return static_cast<std::int64_t>(array->size());
        if (const auto* object = std::get_if<Object>(&value_))
            return static_cast<std::int64_t>(object->size());
        if (const auto* string = std::get_if<std::string>(&value_))
            return static_cast<std::int64_t>(string->size());
        throw SunRuntimeError("len() requires a JSON object, array, or string");
    }
    SunJson& at(const std::string& key) {
        auto* object = std::get_if<Object>(&value_);
        if (!object) throw SunRuntimeError("JSON value is not an object");
        const auto found = object->find(key);
        if (found == object->end()) throw SunRuntimeError("JSON object key not found: " + key);
        return found->second;
    }
    const SunJson& at(const std::string& key) const {
        const auto* object = std::get_if<Object>(&value_);
        if (!object) throw SunRuntimeError("JSON value is not an object");
        const auto found = object->find(key);
        if (found == object->end()) throw SunRuntimeError("JSON object key not found: " + key);
        return found->second;
    }
    SunJson& at(std::int64_t index) {
        auto* array = std::get_if<Array>(&value_);
        if (!array) throw SunRuntimeError("JSON value is not an array");
        if (index < 0 || static_cast<std::uint64_t>(index) >= array->size())
            throw SunRuntimeError("JSON array index out of bounds");
        return (*array)[static_cast<std::size_t>(index)];
    }
    const SunJson& at(std::int64_t index) const {
        const auto* array = std::get_if<Array>(&value_);
        if (!array) throw SunRuntimeError("JSON value is not an array");
        if (index < 0 || static_cast<std::uint64_t>(index) >= array->size())
            throw SunRuntimeError("JSON array index out of bounds");
        return (*array)[static_cast<std::size_t>(index)];
    }
    SunJson& set(const std::string& key, SunJson value) {
        auto* object = std::get_if<Object>(&value_);
        if (!object) throw SunRuntimeError("JSON value is not an object");
        return (*object)[key] = std::move(value);
    }
    SunJson& set(std::int64_t index, SunJson value) {
        auto* array = std::get_if<Array>(&value_);
        if (!array) throw SunRuntimeError("JSON value is not an array");
        if (index < 0 || static_cast<std::uint64_t>(index) >= array->size())
            throw SunRuntimeError("JSON array index out of bounds");
        return (*array)[static_cast<std::size_t>(index)] = std::move(value);
    }
    std::int64_t append(SunJson value) {
        auto* array = std::get_if<Array>(&value_);
        if (!array) throw SunRuntimeError("JSON value is not an array");
        if (array->size() >=
            static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max())) {
            throw SunRuntimeError("JSON array size exceeds int range");
        }
        array->push_back(std::move(value));
        return static_cast<std::int64_t>(array->size());
    }
    bool contains(const std::string& key) const {
        const auto* object = std::get_if<Object>(&value_);
        if (!object) throw SunRuntimeError("JSON value is not an object");
        return object->find(key) != object->end();
    }
    SunJson& at(double index) { return at(sun_to_int(index)); }
    const SunJson& at(double index) const { return at(sun_to_int(index)); }
    SunJson& set(double index, SunJson value) { return set(sun_to_int(index), std::move(value)); }
    friend bool operator==(const SunJson& left, const SunJson& right) {
        const auto* left_parsed = std::get_if<ParsedNumber>(&left.value_);
        const auto* right_parsed = std::get_if<ParsedNumber>(&right.value_);
        if (left_parsed) {
            if (right_parsed)
                return left_parsed->approximation == right_parsed->approximation;
            if (const auto* right_number = std::get_if<double>(&right.value_))
                return left_parsed->approximation == *right_number;
            return false;
        }
        if (right_parsed) {
            if (const auto* left_number = std::get_if<double>(&left.value_))
                return *left_number == right_parsed->approximation;
            return false;
        }
        return left.value_ == right.value_;
    }
    friend bool operator!=(const SunJson& left, const SunJson& right) {
        return !(left == right);
    }

private:
    static std::string dump_string(const std::string& value) {
        std::string result = "\"";
        for (const unsigned char byte : value) {
            switch (byte) {
            case '"': result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\b': result += "\\b"; break;
            case '\f': result += "\\f"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default:
                if (byte < 0x20) {
                    const char* digits = "0123456789abcdef";
                    result += "\\u00";
                    result.push_back(digits[byte >> 4]);
                    result.push_back(digits[byte & 0x0f]);
                } else {
                    result.push_back(static_cast<char>(byte));
                }
            }
        }
        return result + '"';
    }

    static bool parse_int64(const std::string& token, std::int64_t& result) {
        std::size_t position = 0;
        bool negative = false;
        if (token[position] == '-') {
            negative = true;
            ++position;
        }

        std::string digits;
        std::int64_t fractional_digits = 0;
        while (position < token.size() &&
               token[position] >= '0' && token[position] <= '9') {
            digits.push_back(token[position++]);
        }
        if (position < token.size() && token[position] == '.') {
            ++position;
            while (position < token.size() &&
                   token[position] >= '0' && token[position] <= '9') {
                digits.push_back(token[position++]);
                ++fractional_digits;
            }
        }

        std::int64_t exponent = 0;
        if (position < token.size() &&
            (token[position] == 'e' || token[position] == 'E')) {
            ++position;
            bool exponent_negative = false;
            if (position < token.size() &&
                (token[position] == '+' || token[position] == '-')) {
                exponent_negative = token[position] == '-';
                ++position;
            }
            while (position < token.size() &&
                   token[position] >= '0' && token[position] <= '9') {
                exponent = std::min<std::int64_t>(
                    1000000, exponent * 10 + token[position++] - '0');
            }
            if (exponent_negative) exponent = -exponent;
        }

        const auto first_nonzero = digits.find_first_not_of('0');
        if (first_nonzero == std::string::npos) {
            result = 0;
            return true;
        }
        digits.erase(0, first_nonzero);

        const auto scale = exponent - fractional_digits;
        if (scale < 0) {
            const auto trailing_zeros = static_cast<std::size_t>(-scale);
            if (trailing_zeros > digits.size()) return false;
            for (std::size_t i = digits.size() - trailing_zeros;
                 i < digits.size(); ++i) {
                if (digits[i] != '0') return false;
            }
            digits.resize(digits.size() - trailing_zeros);
        } else if (scale > 0) {
            if (scale > 19 ||
                digits.size() + static_cast<std::size_t>(scale) > 19) {
                return false;
            }
            digits.append(static_cast<std::size_t>(scale), '0');
        }
        if (digits.size() > 19) return false;

        const std::string signed_digits = negative ? "-" + digits : digits;
        const auto conversion = std::from_chars(
            signed_digits.data(), signed_digits.data() + signed_digits.size(), result);
        return conversion.ec == std::errc{} &&
               conversion.ptr == signed_digits.data() + signed_digits.size();
    }

    Value value_;
};

std::int64_t sun_json_append(SunJson& array, SunJson value) {
    return array.append(std::move(value));
}

inline SunJson sun_json_from(const SunJson& value) { return value; }
inline SunJson sun_json_from(bool value) { return SunJson(SunJson::Value{value}); }
inline SunJson sun_json_from(double value) {
    if (!std::isfinite(value)) throw SunRuntimeError("json.dumps cannot serialize NaN or infinity");
    return SunJson(SunJson::Value{value});
}
inline SunJson sun_json_from(std::int64_t value) {
    return SunJson(SunJson::Value{value});
}
inline SunJson sun_json_from(const std::string& value) {
    return SunJson(SunJson::Value{value});
}
template <typename T>
SunJson sun_json_from(const std::vector<T>& values) {
    SunJson::Array result;
    result.reserve(values.size());
    for (const auto& value : values) result.push_back(sun_json_from(static_cast<T>(value)));
    return SunJson(SunJson::Value{std::move(result)});
}
template <typename V>
SunJson sun_json_from(const std::map<std::string, V>& values) {
    SunJson::Object result;
    for (const auto& entry : values)
        result.emplace(entry.first, sun_json_from(entry.second));
    return SunJson(SunJson::Value{std::move(result)});
}
inline std::string sun_json_dumps(const SunJson& value) { return value.dump(); }
inline std::string sun_json_dumps(bool value) { return sun_json_from(value).dump(); }
inline std::string sun_json_dumps(double value) { return sun_json_from(value).dump(); }
inline std::string sun_json_dumps(std::int64_t value) { return sun_json_from(value).dump(); }
inline std::string sun_json_dumps(const std::string& value) { return sun_json_from(value).dump(); }
template <typename T>
std::string sun_json_dumps(const std::vector<T>& value) {
    return sun_json_from(value).dump();
}
template <typename V>
std::string sun_json_dumps(const std::map<std::string, V>& value) {
    return sun_json_from(value).dump();
}
inline std::string sun_to_string(const SunJson& value) { return value.dump(); }

SunJson& sun_json_at_ref(SunJson& object, const std::string& key) {
    return object.at(key);
}
SunJson& sun_json_at_ref(SunJson& object, std::int64_t index) {
    return object.at(index);
}
SunJson& sun_json_at_ref(SunJson& object, double index) {
    return object.at(index);
}
const SunJson& sun_json_at_ref(const SunJson& object, const std::string& key) {
    return object.at(key);
}
const SunJson& sun_json_at_ref(const SunJson& object, std::int64_t index) {
    return object.at(index);
}
const SunJson& sun_json_at_ref(const SunJson& object, double index) {
    return object.at(index);
}
SunJson& sun_json_set(SunJson& object, const std::string& key, SunJson value) {
    return object.set(key, std::move(value));
}
SunJson& sun_json_set(SunJson& object, std::int64_t index, SunJson value) {
    return object.set(index, std::move(value));
}
SunJson& sun_json_set(SunJson& object, double index, SunJson value) {
    return object.set(index, std::move(value));
}
)cpp";
}

} // namespace sun
