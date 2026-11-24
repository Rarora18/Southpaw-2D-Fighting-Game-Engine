#include "core/json.h"

#include <cstdlib>
#include <cstring>

namespace sp {
namespace {

struct Parser {
    const std::string& s;
    size_t i = 0;
    std::string err;

    int line() const {
        int n = 1;
        for (size_t k = 0; k < i && k < s.size(); ++k) n += s[k] == '\n';
        return n;
    }

    bool fail(const std::string& msg) {
        if (err.empty()) err = "line " + std::to_string(line()) + ": " + msg;
        return false;
    }

    void skipWhitespace() {
        while (i < s.size()) {
            char c = s[i];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++i;
            } else if (c == '/' && i + 1 < s.size() && s[i + 1] == '/') {
                while (i < s.size() && s[i] != '\n') ++i;
            } else {
                break;
            }
        }
    }

    bool value(Json& out, int depth) {
        if (depth > 64) return fail("nesting too deep");
        skipWhitespace();
        if (i >= s.size()) return fail("unexpected end of input");
        char c = s[i];
        if (c == '{') return object(out, depth);
        if (c == '[') return array(out, depth);
        if (c == '"') {
            out.type = Json::Type::String;
            return string(out.string);
        }
        if (c == 't' || c == 'f' || c == 'n') return literal(out);
        if (c == '-' || (c >= '0' && c <= '9')) return number(out);
        return fail(std::string("unexpected character '") + c + "'");
    }

    bool object(Json& out, int depth) {
        out.type = Json::Type::Object;
        ++i;
        skipWhitespace();
        if (i < s.size() && s[i] == '}') {
            ++i;
            return true;
        }
        for (;;) {
            skipWhitespace();
            if (i >= s.size() || s[i] != '"') return fail("expected string key");
            std::string key;
            if (!string(key)) return false;
            skipWhitespace();
            if (i >= s.size() || s[i] != ':') return fail("expected ':' after key \"" + key + "\"");
            ++i;
            Json v;
            if (!value(v, depth + 1)) return false;
            out.object.emplace_back(std::move(key), std::move(v));
            skipWhitespace();
            if (i < s.size() && s[i] == ',') {
                ++i;
                continue;
            }
            if (i < s.size() && s[i] == '}') {
                ++i;
                return true;
            }
            return fail("expected ',' or '}'");
        }
    }

    bool array(Json& out, int depth) {
        out.type = Json::Type::Array;
        ++i;
        skipWhitespace();
        if (i < s.size() && s[i] == ']') {
            ++i;
            return true;
        }
        for (;;) {
            Json v;
            if (!value(v, depth + 1)) return false;
            out.array.push_back(std::move(v));
            skipWhitespace();
            if (i < s.size() && s[i] == ',') {
                ++i;
                continue;
            }
            if (i < s.size() && s[i] == ']') {
                ++i;
                return true;
            }
            return fail("expected ',' or ']'");
        }
    }

    static void appendUtf8(std::string& out, unsigned cp) {
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    bool string(std::string& out) {
        ++i;  // opening quote
        while (i < s.size()) {
            char c = s[i++];
            if (c == '"') return true;
            if (c != '\\') {
                out += c;
                continue;
            }
            if (i >= s.size()) break;
            char e = s[i++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    if (i + 4 > s.size()) return fail("bad \\u escape");
                    unsigned cp = static_cast<unsigned>(std::strtoul(s.substr(i, 4).c_str(), nullptr, 16));
                    i += 4;
                    appendUtf8(out, cp);
                    break;
                }
                default: return fail(std::string("bad escape '\\") + e + "'");
            }
        }
        return fail("unterminated string");
    }

    bool number(Json& out) {
        const char* begin = s.c_str() + i;
        char* end = nullptr;
        double d = std::strtod(begin, &end);
        if (end == begin) return fail("bad number");
        i += static_cast<size_t>(end - begin);
        out.type = Json::Type::Number;
        out.number = d;
        return true;
    }

    bool literal(Json& out) {
        auto match = [&](const char* word) {
            size_t n = std::strlen(word);
            if (s.compare(i, n, word) != 0) return false;
            i += n;
            return true;
        };
        if (match("true")) {
            out.type = Json::Type::Bool;
            out.boolean = true;
            return true;
        }
        if (match("false")) {
            out.type = Json::Type::Bool;
            out.boolean = false;
            return true;
        }
        if (match("null")) {
            out.type = Json::Type::Null;
            return true;
        }
        return fail("unknown literal");
    }
};

}  // namespace

bool Json::parse(const std::string& text, Json& out, std::string& error) {
    Parser p{text, 0, {}};
    Json v;
    if (!p.value(v, 0)) {
        error = p.err;
        return false;
    }
    p.skipWhitespace();
    if (p.i != text.size()) {
        p.fail("trailing characters after JSON value");
        error = p.err;
        return false;
    }
    out = std::move(v);
    return true;
}

const Json* Json::get(const std::string& key) const {
    if (type != Type::Object) return nullptr;
    for (const auto& kv : object)
        if (kv.first == key) return &kv.second;
    return nullptr;
}

double Json::num(const std::string& key, double fallback) const {
    const Json* v = get(key);
    return v && v->type == Type::Number ? v->number : fallback;
}

std::string Json::str(const std::string& key, const std::string& fallback) const {
    const Json* v = get(key);
    return v && v->type == Type::String ? v->string : fallback;
}

bool Json::flag(const std::string& key, bool fallback) const {
    const Json* v = get(key);
    return v && v->type == Type::Bool ? v->boolean : fallback;
}

}  // namespace sp
