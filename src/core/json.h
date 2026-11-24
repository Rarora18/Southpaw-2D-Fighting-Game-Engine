#pragma once
// Small JSON reader for move data. Accepts standard JSON plus // line comments
// so designers can annotate frame data.
#include <string>
#include <utility>
#include <vector>

namespace sp {

class Json {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Type type = Type::Null;
    bool boolean = false;
    double number = 0.0;
    std::string string;
    std::vector<Json> array;
    std::vector<std::pair<std::string, Json>> object;

    static bool parse(const std::string& text, Json& out, std::string& error);

    bool isNumber() const { return type == Type::Number; }
    bool isString() const { return type == Type::String; }
    bool isArray() const { return type == Type::Array; }
    bool isObject() const { return type == Type::Object; }

    const Json* get(const std::string& key) const;
    double num(const std::string& key, double fallback) const;
    std::string str(const std::string& key, const std::string& fallback) const;
    bool flag(const std::string& key, bool fallback) const;
};

}  // namespace sp
