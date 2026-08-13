#include "core/config/Json.hpp"

#include <cctype>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <variant>

namespace pr {

namespace {

void appendCodepointUtf8(std::string& out, unsigned codepoint) {
    if (codepoint <= 0x7F) {
        out.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
}

int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

unsigned parseUnicodeEscapeDigits(const std::string& text, std::size_t& pos) {
    if (pos + 4 > text.size()) throw std::runtime_error("Bad JSON unicode escape");
    unsigned codepoint = 0;
    for (int i = 0; i < 4; ++i) {
        const int value = hexValue(text[pos++]);
        if (value < 0) throw std::runtime_error("Bad JSON unicode escape");
        codepoint = (codepoint << 4) | static_cast<unsigned>(value);
    }
    return codepoint;
}

} // namespace

bool JsonValue::isObject() const { return std::holds_alternative<Object>(value_); }
bool JsonValue::isArray() const { return std::holds_alternative<Array>(value_); }
bool JsonValue::isString() const { return std::holds_alternative<std::string>(value_); }
bool JsonValue::isNumber() const { return std::holds_alternative<double>(value_); }
bool JsonValue::isBool() const { return std::holds_alternative<bool>(value_); }
bool JsonValue::isNull() const { return std::holds_alternative<std::nullptr_t>(value_); }
JsonValue::Object& JsonValue::asObject() { return std::get<Object>(value_); }
const JsonValue::Object& JsonValue::asObject() const { return std::get<Object>(value_); }
JsonValue::Array& JsonValue::asArray() { return std::get<Array>(value_); }
const JsonValue::Array& JsonValue::asArray() const { return std::get<Array>(value_); }
std::string& JsonValue::asString() { return std::get<std::string>(value_); }
const std::string& JsonValue::asString() const { return std::get<std::string>(value_); }
double JsonValue::asNumber() const { return std::get<double>(value_); }
bool JsonValue::asBool() const { return std::get<bool>(value_); }
JsonValue* JsonValue::get(const std::string& key) {
    if (!isObject()) return nullptr;
    auto it = asObject().find(key);
    return it == asObject().end() ? nullptr : &it->second;
}
const JsonValue* JsonValue::get(const std::string& key) const {
    if (!isObject()) return nullptr;
    auto it = asObject().find(key);
    return it == asObject().end() ? nullptr : &it->second;
}
JsonValue& JsonValue::operator[](const std::string& key) {
    if (isNull()) {
        value_ = Object{};
    }
    if (!isObject()) {
        throw std::runtime_error("JSON value is not an object");
    }
    return asObject()[key];
}

class Parser {
public:
    explicit Parser(std::string text) : text_(std::move(text)) {}

    JsonValue parse() {
        skipWhitespace();
        JsonValue value = parseValue();
        skipWhitespace();
        if (pos_ != text_.size()) throw std::runtime_error("Unexpected trailing JSON content");
        return value;
    }

private:
    JsonValue parseValue() {
        skipWhitespace();
        if (pos_ >= text_.size()) throw std::runtime_error("Unexpected end of JSON");
        char c = text_[pos_];
        if (c == '{') return parseObject();
        if (c == '[') return parseArray();
        if (c == '"') return JsonValue(parseString());
        if (c == 't') return parseLiteral("true", JsonValue(true));
        if (c == 'f') return parseLiteral("false", JsonValue(false));
        if (c == 'n') return parseLiteral("null", JsonValue(nullptr));
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) return JsonValue(parseNumber());
        throw std::runtime_error("Unexpected JSON token");
    }

    JsonValue parseObject() {
        expect('{');
        JsonValue::Object object;
        skipWhitespace();
        if (peek('}')) { expect('}'); return JsonValue(object); }
        while (true) {
            std::string key = parseString();
            skipWhitespace();
            expect(':');
            object.emplace(std::move(key), parseValue());
            skipWhitespace();
            if (peek('}')) { expect('}'); break; }
            expect(',');
            skipWhitespace();
        }
        return JsonValue(object);
    }

    JsonValue parseArray() {
        expect('[');
        JsonValue::Array array;
        skipWhitespace();
        if (peek(']')) { expect(']'); return JsonValue(array); }
        while (true) {
            array.push_back(parseValue());
            skipWhitespace();
            if (peek(']')) { expect(']'); break; }
            expect(',');
            skipWhitespace();
        }
        return JsonValue(array);
    }

    std::string parseString() {
        expect('"');
        std::string out;
        while (pos_ < text_.size()) {
            char c = text_[pos_++];
            if (c == '"') return out;
            if (c == '\\') {
                if (pos_ >= text_.size()) throw std::runtime_error("Bad JSON escape");
                char e = text_[pos_++];
                switch (e) {
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/': out.push_back('/'); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case 'n': out.push_back('\n'); break;
                    case 'r': out.push_back('\r'); break;
                    case 't': out.push_back('\t'); break;
                    case 'u': {
                        unsigned codepoint = parseUnicodeEscapeDigits(text_, pos_);
                        if (codepoint >= 0xD800 && codepoint <= 0xDBFF) {
                            if (pos_ + 2 > text_.size() || text_[pos_] != '\\' || text_[pos_ + 1] != 'u') {
                                throw std::runtime_error("Bad JSON unicode surrogate pair");
                            }
                            pos_ += 2;
                            const unsigned low = parseUnicodeEscapeDigits(text_, pos_);
                            if (low < 0xDC00 || low > 0xDFFF) {
                                throw std::runtime_error("Bad JSON unicode surrogate pair");
                            }
                            codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + (low - 0xDC00);
                        } else if (codepoint >= 0xDC00 && codepoint <= 0xDFFF) {
                            throw std::runtime_error("Bad JSON unicode surrogate pair");
                        }
                        appendCodepointUtf8(out, codepoint);
                        break;
                    }
                    default: throw std::runtime_error("Unsupported JSON escape");
                }
            } else {
                if (static_cast<unsigned char>(c) < 0x20U) {
                    throw std::runtime_error("Unescaped control character in JSON string");
                }
                out.push_back(c);
            }
        }
        throw std::runtime_error("Unterminated JSON string");
    }

    double parseNumber() {
        std::size_t start = pos_;
        if (text_[pos_] == '-') ++pos_;
        if (pos_ >= text_.size()) throw std::runtime_error("Invalid JSON number");
        if (text_[pos_] == '0') {
            ++pos_;
            if (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_]))) {
                throw std::runtime_error("Invalid JSON number with leading zero");
            }
        } else {
            if (!std::isdigit(static_cast<unsigned char>(text_[pos_]))) {
                throw std::runtime_error("Invalid JSON number");
            }
            while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_]))) ++pos_;
        }
        if (pos_ < text_.size() && text_[pos_] == '.') {
            ++pos_;
            const std::size_t fraction_start = pos_;
            while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_]))) ++pos_;
            if (fraction_start == pos_) throw std::runtime_error("Invalid JSON number fraction");
        }
        if (pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E')) {
            ++pos_;
            if (pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-')) ++pos_;
            const std::size_t exponent_start = pos_;
            while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_]))) ++pos_;
            if (exponent_start == pos_) throw std::runtime_error("Invalid JSON number exponent");
        }
        const double value = std::stod(text_.substr(start, pos_ - start));
        if (!std::isfinite(value)) throw std::runtime_error("JSON number is outside the finite range");
        return value;
    }

    JsonValue parseLiteral(const std::string& literal, JsonValue value) {
        if (text_.compare(pos_, literal.size(), literal) != 0) throw std::runtime_error("Invalid JSON literal");
        pos_ += literal.size();
        return value;
    }

    void skipWhitespace() {
        while (pos_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[pos_]))) ++pos_;
    }

    void expect(char c) {
        if (pos_ >= text_.size() || text_[pos_] != c) throw std::runtime_error("Unexpected JSON character");
        ++pos_;
    }

    bool peek(char c) const {
        return pos_ < text_.size() && text_[pos_] == c;
    }

    std::string text_;
    std::size_t pos_ = 0;
};

JsonValue parseJsonFile(const std::string& path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("Could not open JSON file: " + path);
    std::stringstream buffer;
    buffer << input.rdbuf();
    return Parser(buffer.str()).parse();
}

JsonValue parseJsonText(const std::string& text) {
    return Parser(text).parse();
}

namespace {

void appendIndent(std::string& out, std::size_t depth, std::size_t indent_size) {
    out.append(depth * indent_size, ' ');
}

void appendEscapedString(std::string& out, const std::string& value) {
    static constexpr char kHex[] = "0123456789abcdef";
    out.push_back('"');
    for (const unsigned char c : value) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20U) {
                    out += "\\u00";
                    out.push_back(kHex[(c >> 4U) & 0x0FU]);
                    out.push_back(kHex[c & 0x0FU]);
                } else {
                    out.push_back(static_cast<char>(c));
                }
                break;
        }
    }
    out.push_back('"');
}

void appendJson(
    std::string& out,
    const JsonValue& value,
    JsonStyle style,
    std::size_t indent_size,
    std::size_t depth) {
    const bool pretty = style == JsonStyle::Pretty;
    if (value.isNull()) {
        out += "null";
    } else if (value.isBool()) {
        out += value.asBool() ? "true" : "false";
    } else if (value.isNumber()) {
        const double number = value.asNumber();
        if (!std::isfinite(number)) {
            throw std::runtime_error("Cannot serialize a non-finite JSON number");
        }
        std::ostringstream number_stream;
        number_stream.imbue(std::locale::classic());
        number_stream << std::setprecision(std::numeric_limits<double>::max_digits10) << number;
        out += number_stream.str();
    } else if (value.isString()) {
        appendEscapedString(out, value.asString());
    } else if (value.isArray()) {
        const auto& array = value.asArray();
        if (array.empty()) {
            out += "[]";
            return;
        }
        out.push_back('[');
        if (pretty) out.push_back('\n');
        for (std::size_t i = 0; i < array.size(); ++i) {
            if (pretty) appendIndent(out, depth + 1U, indent_size);
            appendJson(out, array[i], style, indent_size, depth + 1U);
            if (i + 1U < array.size()) out.push_back(',');
            if (pretty) out.push_back('\n');
        }
        if (pretty) appendIndent(out, depth, indent_size);
        out.push_back(']');
    } else {
        const auto& object = value.asObject();
        if (object.empty()) {
            out += "{}";
            return;
        }
        out.push_back('{');
        if (pretty) out.push_back('\n');
        std::size_t index = 0;
        for (const auto& [key, item] : object) {
            if (pretty) appendIndent(out, depth + 1U, indent_size);
            appendEscapedString(out, key);
            out += pretty ? ": " : ":";
            appendJson(out, item, style, indent_size, depth + 1U);
            if (++index < object.size()) out.push_back(',');
            if (pretty) out.push_back('\n');
        }
        if (pretty) appendIndent(out, depth, indent_size);
        out.push_back('}');
    }
}

} // namespace

std::string serializeJsonValue(const JsonValue& value, JsonStyle style, std::size_t indent_size) {
    std::string out;
    appendJson(out, value, style, indent_size, 0);
    return out;
}

} // namespace pr
