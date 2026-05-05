#pragma once

#include <cstddef>
#include <map>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace workflow::json
{

class JsonParseError : public std::runtime_error
{
  public:
    explicit JsonParseError(const std::string& message)
        : std::runtime_error(message)
    {
    }
};

class Value
{
  public:
    using Object = std::map<std::string, Value>;
    using Array = std::vector<Value>;

    Value();
    Value(std::nullptr_t);
    Value(bool value);
    Value(int value);
    Value(double value);
    Value(std::string value);
    Value(const char* value);
    Value(Object value);
    Value(Array value);

    bool isNull() const;
    bool isBool() const;
    bool isInt() const;
    bool isDouble() const;
    bool isNumber() const;
    bool isString() const;
    bool isObject() const;
    bool isArray() const;

    bool asBool() const;
    int asInt() const;
    double asDouble() const;
    const std::string& asString() const;
    const Object& asObject() const;
    const Array& asArray() const;

    Object& asObject();
    Array& asArray();

    bool contains(const std::string& key) const;

    const Value& at(const std::string& key) const;
    Value& at(const std::string& key);

    const Value& operator[](const std::string& key) const;
    Value& operator[](const std::string& key);

    const Value& operator[](std::size_t index) const;
    Value& operator[](std::size_t index);

    std::size_t size() const;
    bool empty() const;

    void erase(const std::string& key);

    static Value object();
    static Value array();

  private:
    using Storage = std::variant<std::nullptr_t, bool, int, double, std::string, Object, Array>;

    Storage value_;
};

Value parse(const std::string& text);

std::string stringify(const Value& value);

} // namespace workflow::json
