#include "lexer.hpp"

namespace sun {
namespace {

Keyword keyword_from_word(const std::string& word) {
    if (word == "def") return Keyword::def;
    if (word == "class") return Keyword::class_;
    if (word == "struct") return Keyword::struct_;
    if (word == "enum") return Keyword::enum_;
    if (word == "if") return Keyword::if_;
    if (word == "elif") return Keyword::elif_;
    if (word == "else") return Keyword::else_;
    if (word == "while") return Keyword::while_;
    if (word == "for") return Keyword::for_;
    if (word == "in") return Keyword::in_;
    if (word == "match") return Keyword::match_;
    if (word == "case") return Keyword::case_;
    if (word == "try") return Keyword::try_;
    if (word == "with") return Keyword::with_;
    if (word == "catch") return Keyword::catch_;
    if (word == "except") return Keyword::except_;
    if (word == "finally") return Keyword::finally_;
    if (word == "export") return Keyword::export_;
    if (word == "let") return Keyword::let_;
    if (word == "return") return Keyword::return_;
    if (word == "break") return Keyword::break_;
    if (word == "continue") return Keyword::continue_;
    if (word == "import") return Keyword::import_;
    if (word == "end") return Keyword::end_;
    if (word == "print") return Keyword::print_;
    if (word == "assert") return Keyword::assert_;
    if (word == "const") return Keyword::const_;
    if (word == "raise") return Keyword::raise_;
    if (word == "true") return Keyword::true_;
    if (word == "false") return Keyword::false_;
    if (word == "and") return Keyword::and_;
    if (word == "or") return Keyword::or_;
    if (word == "not") return Keyword::not_;
    return Keyword::none;
}

bool is_identifier_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

bool is_identifier_char(char c) {
    return is_identifier_start(c) || (c >= '0' && c <= '9');
}

} // namespace

Keyword keyword_at_start(const std::string& source) {
    std::size_t start = 0;
    while (start < source.size() &&
           (source[start] == ' ' || source[start] == '\t' || source[start] == '\r')) {
        ++start;
    }
    if (start == source.size() || !is_identifier_start(source[start])) {
        return Keyword::none;
    }
    auto end = start + 1;
    while (end < source.size() && is_identifier_char(source[end])) ++end;
    return keyword_from_word(source.substr(start, end - start));
}

std::vector<Token> tokenize(const std::string& source, std::size_t line) {
    std::vector<Token> tokens;
    std::size_t i = 0;
    while (i < source.size()) {
        const char c = source[i];
        if (c == ' ' || c == '\t' || c == '\r') {
            ++i;
            continue;
        }
        const bool fstring = c == 'f' && i + 1 < source.size() && source[i + 1] == '"';
        if (c == '"' || fstring) {
            if (fstring) ++i;
            ++i;
            std::string value;
            bool closed = false;
            while (i < source.size()) {
                char current = source[i++];
                if (current == '"') {
                    closed = true;
                    break;
                }
                if (current == '\\') {
                    if (i == source.size()) {
                        throw ParseError(line, "unterminated escape sequence");
                    }
                    current = source[i++];
                    switch (current) {
                    case '0': value.push_back('\0'); break;
                    case 'n': value.push_back('\n'); break;
                    case 't': value.push_back('\t'); break;
                    case 'r': value.push_back('\r'); break;
                    case '\\': value.push_back('\\'); break;
                    case '"': value.push_back('"'); break;
                    default: throw ParseError(line, "unsupported escape sequence");
                    }
                } else {
                    if (current == '\0') {
                        throw ParseError(line, "NUL byte is not allowed in string literal");
                    }
                    value.push_back(current);
                }
            }
            if (!closed) throw ParseError(line, "unterminated string literal");
            tokens.push_back({Token::Kind::string, value, fstring});
            continue;
        }
        if (is_identifier_start(c)) {
            const auto start = i++;
            while (i < source.size() && is_identifier_char(source[i])) ++i;
            const auto text = source.substr(start, i - start);
            tokens.push_back({Token::Kind::identifier, text, false,
                              keyword_from_word(text)});
            continue;
        }
        if (c >= '0' && c <= '9') {
            const auto start = i++;
            while (i < source.size() && source[i] >= '0' && source[i] <= '9') ++i;
            if (i < source.size() && source[i] == '.') {
                ++i;
                while (i < source.size() && source[i] >= '0' && source[i] <= '9') ++i;
            }
            if (i < source.size() && (source[i] == 'e' || source[i] == 'E')) {
                ++i;
                if (i < source.size() && (source[i] == '+' || source[i] == '-')) ++i;
                const auto exponent_start = i;
                while (i < source.size() && source[i] >= '0' && source[i] <= '9') ++i;
                if (i == exponent_start) throw ParseError(line, "malformed number exponent");
            }
            tokens.push_back({Token::Kind::number, source.substr(start, i - start)});
            continue;
        }
        if (i + 1 < source.size()) {
            const auto pair = source.substr(i, 2);
            if (pair == "==" || pair == "!=" || pair == "<=" || pair == ">=" ||
                pair == "**") {
                tokens.push_back({Token::Kind::symbol, pair});
                i += 2;
                continue;
            }
        }
        if (std::string("+-*/%<>()=,.[]{}:").find(c) != std::string::npos) {
            tokens.push_back({Token::Kind::symbol, std::string(1, c)});
            ++i;
            continue;
        }
        throw ParseError(line, "unexpected character in expression");
    }
    tokens.push_back({Token::Kind::end, ""});
    return tokens;
}

} // namespace sun
