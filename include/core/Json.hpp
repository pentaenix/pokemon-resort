#pragma once

#include <map>
#include <string>
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

    const Object& asObject() const;
    const Array& asArray() const;
    const std::string& asString() const;
    double asNumber() const;
    bool asBool() const;

    const JsonValue* get(const std::string& key) const;

private:
    Value value_ = nullptr;
};

JsonValue parseJsonFile(const std::string& path);
JsonValue parseJsonText(const std::string& text);

} // namespace pr
