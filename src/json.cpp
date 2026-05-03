#include "workflow/json.hpp"

#include <cctype>
#include <charconv>
#include <sstream>

namespace workflow::json
{

Value::Value()
    : value_(nullptr)
{
}

Value::Value(std::nullptr_t)
    : value_(nullptr)
{
}

Value::Value(bool value)
    : value_(value)
{
}

Value::Value(int value)
    : value_(value)
{
}

Value::Value(double value)
    : value_(value)
{
}

Value::Value(std::string value)
    : value_(std::move(value))
{
}

Value::Value(const char* value)
    : value_(std::string(value))
{
}

Value::Value(Object value)
    : value_(std::move(value))
{
}

Value::Value(Array value)
    : value_(std::move(value))
{
}

bool Value::isNull() const
{
    return std::holds_alternative<std::nullptr_t>(value_);
}

bool Value::isBool() const
{
    return std::holds_alternative<bool>(value_);
}

bool Value::isInt() const
{
    return std::holds_alternative<int>(value_);
}

bool Value::isDouble() const
{
    return std::holds_alternative<double>(value_);
}

bool Value::isNumber() const
{
    return isInt() || isDouble();
}

bool Value::isString() const
{
    return std::holds_alternative<std::string>(value_);
}

bool Value::isObject() const
{
    return std::holds_alternative<Object>(value_);
}

bool Value::isArray() const
{
    return std::holds_alternative<Array>(value_);
}

bool Value::asBool() const
{
    return std::get<bool>(value_);
}

int Value::asInt() const
{
    return std::get<int>(value_);
}

double Value::asDouble() const
{
    if (isInt())
    {
        return static_cast<double>(asInt());
    }
    return std::get<double>(value_);
}

const std::string& Value::asString() const
{
    return std::get<std::string>(value_);
}

const Value::Object& Value::asObject() const
{
    return std::get<Object>(value_);
}

const Value::Array& Value::asArray() const
{
    return std::get<Array>(value_);
}

Value::Object& Value::asObject()
{
    return std::get<Object>(value_);
}

Value::Array& Value::asArray()
{
    return std::get<Array>(value_);
}

bool Value::contains(const std::string& key) const
{
    if (!isObject())
    {
        return false;
    }

    return asObject().contains(key);
}

const Value& Value::at(const std::string& key) const
{
    return asObject().at(key);
}

Value& Value::at(const std::string& key)
{
    return asObject().at(key);
}

const Value& Value::operator[](const std::string& key) const
{
    return at(key);
}

Value& Value::operator[](const std::string& key)
{
    return asObject()[key];
}

const Value& Value::operator[](std::size_t index) const
{
    return asArray().at(index);
}

Value& Value::operator[](std::size_t index)
{
    return asArray().at(index);
}

std::size_t Value::size() const
{
    if (isObject())
    {
        return asObject().size();
    }

    if (isArray())
    {
        return asArray().size();
    }

    return 0;
}

bool Value::empty() const
{
    return size() == 0;
}

void Value::erase(const std::string& key)
{
    asObject().erase(key);
}

Value Value::object()
{
    return Value(Object{});
}

Value Value::array()
{
    return Value(Array{});
}

namespace
{

class Parser
{
  public:
    explicit Parser(const std::string& text)
        : text_(text)
    {
    }

    Value parseDocument()
    {
        skipWhitespace();
        Value value = parseValue();
        skipWhitespace();

        if (!isAtEnd())
        {
            throw error("unexpected trailing characters");
        }

        return value;
    }

  private:
    const std::string& text_;
    std::size_t pos_ = 0;

    bool isAtEnd() const
    {
        return pos_ >= text_.size();
    }

    char peek() const
    {
        if (isAtEnd())
        {
            return '\0';
        }

        return text_[pos_];
    }

    char advance()
    {
        if (isAtEnd())
        {
            throw error("unexpected end of input");
        }

        return text_[pos_++];
    }

    bool match(char expected)
    {
        if (peek() != expected)
        {
            return false;
        }

        ++pos_;
        return true;
    }

    void expect(char expected)
    {
        if (!match(expected))
        {
            std::string message = "expected '";
            message.push_back(expected);
            message.push_back('\'');
            throw error(message);
        }
    }

    void skipWhitespace()
    {
        while (!isAtEnd())
        {
            const auto ch = static_cast<unsigned char>(peek());

            if (!std::isspace(ch))
            {
                return;
            }

            ++pos_;
        }
    }

    JsonParseError error(const std::string& message) const
    {
        std::ostringstream out;
        out << "JSON parse error at offset " << pos_ << ": " << message;
        return JsonParseError(out.str());
    }

    Value parseValue()
    {
        skipWhitespace();

        switch (peek())
        {
        case '{':
            return parseObject();
        case '[':
            return parseArray();
        case '"':
            return Value(parseString());
        case 't':
            parseLiteral("true");
            return Value(true);
        case 'f':
            parseLiteral("false");
            return Value(false);
        case 'n':
            parseLiteral("null");
            return Value(nullptr);
        default:
            if (peek() == '-' || std::isdigit(static_cast<unsigned char>(peek())))
            {
                return parseNumber();
            }

            throw error("expected JSON value");
        }
    }

    Value parseObject()
    {
        expect('{');
        skipWhitespace();

        Value::Object object;

        if (match('}'))
        {
            return Value(std::move(object));
        }

        while (true)
        {
            skipWhitespace();

            if (peek() != '"')
            {
                throw error("expected object key string");
            }

            std::string key = parseString();

            skipWhitespace();
            expect(':');

            Value value = parseValue();
            object.emplace(std::move(key), std::move(value));

            skipWhitespace();

            if (match('}'))
            {
                break;
            }

            expect(',');
        }

        return Value(std::move(object));
    }

    Value parseArray()
    {
        expect('[');
        skipWhitespace();

        Value::Array array;

        if (match(']'))
        {
            return Value(std::move(array));
        }

        while (true)
        {
            array.push_back(parseValue());

            skipWhitespace();

            if (match(']'))
            {
                break;
            }

            expect(',');
        }

        return Value(std::move(array));
    }

    std::string parseString()
    {
        expect('"');

        std::string result;

        while (!isAtEnd())
        {
            char ch = advance();

            if (ch == '"')
            {
                return result;
            }

            if (ch == '\\')
            {
                result.push_back(parseEscape());
                continue;
            }

            if (static_cast<unsigned char>(ch) < 0x20)
            {
                throw error("unescaped control character in string");
            }

            result.push_back(ch);
        }

        throw error("unterminated string");
    }

    char parseEscape()
    {
        if (isAtEnd())
        {
            throw error("unterminated escape sequence");
        }

        const char escaped = advance();

        switch (escaped)
        {
        case '"':
            return '"';
        case '\\':
            return '\\';
        case '/':
            return '/';
        case 'b':
            return '\b';
        case 'f':
            return '\f';
        case 'n':
            return '\n';
        case 'r':
            return '\r';
        case 't':
            return '\t';
        case 'u':
            throw error("unicode escape sequences are not supported by this parser");
        default:
            throw error("invalid escape sequence");
        }
    }

    void parseLiteral(const std::string& literal)
    {
        for (char expected : literal)
        {
            if (advance() != expected)
            {
                throw error("invalid literal");
            }
        }
    }

    Value parseNumber()
    {
        const std::size_t start = pos_;

        if (match('-'))
        {
            if (!std::isdigit(static_cast<unsigned char>(peek())))
            {
                throw error("expected digit after minus sign");
            }
        }

        if (match('0'))
        {
            // Leading zero is only valid when the number is exactly 0 or followed by
            // fraction/exponent.
        }
        else
        {
            if (!std::isdigit(static_cast<unsigned char>(peek())))
            {
                throw error("expected digit");
            }

            while (std::isdigit(static_cast<unsigned char>(peek())))
            {
                advance();
            }
        }

        bool isFloatingPoint = false;

        if (match('.'))
        {
            isFloatingPoint = true;

            if (!std::isdigit(static_cast<unsigned char>(peek())))
            {
                throw error("expected digit after decimal point");
            }

            while (std::isdigit(static_cast<unsigned char>(peek())))
            {
                advance();
            }
        }

        if (peek() == 'e' || peek() == 'E')
        {
            isFloatingPoint = true;
            advance();

            if (peek() == '+' || peek() == '-')
            {
                advance();
            }

            if (!std::isdigit(static_cast<unsigned char>(peek())))
            {
                throw error("expected exponent digit");
            }

            while (std::isdigit(static_cast<unsigned char>(peek())))
            {
                advance();
            }
        }

        const auto token = text_.substr(start, pos_ - start);

        if (isFloatingPoint)
        {
            char* end = nullptr;
            const double value = std::strtod(token.c_str(), &end);

            if (end == nullptr || *end != '\0')
            {
                throw error("invalid floating point number");
            }

            return Value(value);
        }

        int value = 0;
        const auto begin = token.data();
        const auto end = token.data() + token.size();

        const auto [ptr, ec] = std::from_chars(begin, end, value);

        if (ec != std::errc{} || ptr != end)
        {
            throw error("invalid integer number");
        }

        return Value(value);
    }
};

} // namespace

Value parse(const std::string& text)
{
    Parser parser(text);
    return parser.parseDocument();
}

} // namespace workflow::json
