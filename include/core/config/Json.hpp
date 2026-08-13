#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace pr {

class JsonValue {
public:
    using Object = std::map<std::string, JsonValue>;
    using Array = std::vector<JsonValue>;
    using Value = std::variant<std::nullptr_t, bool, double, std::string, Array, Object>;

    JsonValue() = default;
    explicit JsonValue(Value value) : value_(std::move(value)) {}

    bool isObject() const;
    bool isArray() const;
    bool isString() const;
    bool isNumber() const;
    bool isBool() const;
    bool isNull() const;

    Object& asObject();
    const Object& asObject() const;
    Array& asArray();
    const Array& asArray() const;
    std::string& asString();
    const std::string& asString() const;
    double asNumber() const;
    bool asBool() const;

    JsonValue* get(const std::string& key);
    const JsonValue* get(const std::string& key) const;
    JsonValue& operator[](const std::string& key);

    Value& value() { return value_; }
    const Value& value() const { return value_; }

    friend bool operator==(const JsonValue& lhs, const JsonValue& rhs) {
        return lhs.value_ == rhs.value_;
    }
    friend bool operator!=(const JsonValue& lhs, const JsonValue& rhs) {
        return !(lhs == rhs);
    }

private:
    Value value_ = nullptr;
};

JsonValue parseJsonFile(const std::string& path);
JsonValue parseJsonText(const std::string& text);

enum class JsonStyle {
    Compact,
    Pretty,
};

// Serializes finite JSON values deterministically. Objects retain JsonValue's
// std::map key order; pretty output uses the requested spaces per nesting level.
std::string serializeJsonValue(
    const JsonValue& value,
    JsonStyle style = JsonStyle::Compact,
    std::size_t indent_size = 2);

} // namespace pr
