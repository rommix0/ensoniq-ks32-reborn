#include "player/json.h"

#include <cstddef>
#include <cstdlib>

namespace ens::player {

const JsonValue* JsonValue::find(const std::string& key) const {
    if (type != Type::Object) return nullptr;
    for (const auto& kv : obj)
        if (kv.first == key) return &kv.second;
    return nullptr;
}

namespace {

constexpr int kMaxDepth = 64;

class Parser {
public:
    Parser(const std::string& t, std::string& e) : s_(t), err_(e) {}

    bool run(JsonValue& out) {
        skipWs();
        if (!value(out, 0)) return false;
        skipWs();
        if (pos_ != s_.size()) return fail("trailing characters");
        return true;
    }

private:
    const std::string& s_;
    std::string& err_;
    size_t pos_ = 0;

    bool fail(const char* msg) {
        if (err_.empty()) err_ = std::string(msg) + " at offset " + std::to_string(pos_);
        return false;
    }
    bool atEnd() const { return pos_ >= s_.size(); }
    bool isDigit() const { return !atEnd() && s_[pos_] >= '0' && s_[pos_] <= '9'; }
    void skipWs() {
        while (!atEnd() && (s_[pos_] == ' ' || s_[pos_] == '\t' || s_[pos_] == '\n' || s_[pos_] == '\r')) ++pos_;
    }
    bool literal(const char* word) {
        size_t i = 0;
        for (; word[i]; ++i)
            if (pos_ + i >= s_.size() || s_[pos_ + i] != word[i]) return false;
        pos_ += i;
        return true;
    }

    bool value(JsonValue& v, int depth) {
        if (depth > kMaxDepth) return fail("nesting too deep");
        if (atEnd()) return fail("unexpected end of input");
        const char c = s_[pos_];
        if (c == '{') return object(v, depth);
        if (c == '[') return array(v, depth);
        if (c == '"') {
            v.type = JsonValue::Type::String;
            return string(v.str);
        }
        if (c == 't') {
            if (!literal("true")) return fail("bad literal");
            v.type = JsonValue::Type::Bool;
            v.b = true;
            return true;
        }
        if (c == 'f') {
            if (!literal("false")) return fail("bad literal");
            v.type = JsonValue::Type::Bool;
            v.b = false;
            return true;
        }
        if (c == 'n') {
            if (!literal("null")) return fail("bad literal");
            v.type = JsonValue::Type::Null;
            return true;
        }
        if (c == '-' || (c >= '0' && c <= '9')) return number(v);
        return fail("unexpected character");
    }

    bool number(JsonValue& v) {
        const size_t start = pos_;
        if (s_[pos_] == '-') ++pos_;
        if (!isDigit()) return fail("bad number");
        if (s_[pos_] == '0') {
            ++pos_;
        } else {
            while (isDigit()) ++pos_;
        }
        if (!atEnd() && s_[pos_] == '.') {
            ++pos_;
            if (!isDigit()) return fail("bad number");
            while (isDigit()) ++pos_;
        }
        if (!atEnd() && (s_[pos_] == 'e' || s_[pos_] == 'E')) {
            ++pos_;
            if (!atEnd() && (s_[pos_] == '+' || s_[pos_] == '-')) ++pos_;
            if (!isDigit()) return fail("bad number");
            while (isDigit()) ++pos_;
        }
        v.type = JsonValue::Type::Number;
        v.num = std::strtod(s_.substr(start, pos_ - start).c_str(), nullptr);  // does not throw
        return true;
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
        out.clear();
        ++pos_;  // opening quote
        for (;;) {
            if (atEnd()) return fail("unterminated string");
            const char c = s_[pos_++];
            if (c == '"') return true;
            if (static_cast<unsigned char>(c) < 0x20) return fail("control character in string");
            if (c != '\\') {
                out += c;
                continue;
            }
            if (atEnd()) return fail("unterminated string");
            const char e = s_[pos_++];
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
                    unsigned cp = 0;
                    for (int i = 0; i < 4; ++i) {
                        if (atEnd()) return fail("bad \\u escape");
                        const char h = s_[pos_++];
                        unsigned d;
                        if (h >= '0' && h <= '9') d = static_cast<unsigned>(h - '0');
                        else if (h >= 'a' && h <= 'f') d = static_cast<unsigned>(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') d = static_cast<unsigned>(h - 'A' + 10);
                        else return fail("bad \\u escape");
                        cp = (cp << 4) | d;
                    }
                    if (cp >= 0xD800 && cp <= 0xDFFF) cp = 0xFFFD;  // BMP only: no surrogate pairs
                    appendUtf8(out, cp);
                    break;
                }
                default: return fail("bad escape");
            }
        }
    }

    bool array(JsonValue& v, int depth) {
        v.type = JsonValue::Type::Array;
        ++pos_;  // [
        skipWs();
        if (!atEnd() && s_[pos_] == ']') {
            ++pos_;
            return true;
        }
        for (;;) {
            skipWs();
            v.arr.emplace_back();
            if (!value(v.arr.back(), depth + 1)) return false;
            skipWs();
            if (atEnd()) return fail("unterminated array");
            const char c = s_[pos_++];
            if (c == ']') return true;
            if (c != ',') return fail("expected comma or closing bracket");
        }
    }

    bool object(JsonValue& v, int depth) {
        v.type = JsonValue::Type::Object;
        ++pos_;  // {
        skipWs();
        if (!atEnd() && s_[pos_] == '}') {
            ++pos_;
            return true;
        }
        for (;;) {
            skipWs();
            if (atEnd() || s_[pos_] != '"') return fail("expected object key");
            std::string key;
            if (!string(key)) return false;
            skipWs();
            if (atEnd() || s_[pos_] != ':') return fail("expected colon");
            ++pos_;
            skipWs();
            v.obj.emplace_back(std::move(key), JsonValue());
            if (!value(v.obj.back().second, depth + 1)) return false;
            skipWs();
            if (atEnd()) return fail("unterminated object");
            const char c = s_[pos_++];
            if (c == '}') return true;
            if (c != ',') return fail("expected comma or closing brace");
        }
    }
};

}  // namespace

bool parseJson(const std::string& text, JsonValue& out, std::string& err) {
    err.clear();
    out = JsonValue();
    Parser p(text, err);
    if (!p.run(out)) {
        if (err.empty()) err = "parse error";
        return false;
    }
    return true;
}

bool jsonInt(const JsonValue* v, long long lo, long long hi, int& out) {
    if (!v || v->type != JsonValue::Type::Number) return false;
    if (!(v->num >= static_cast<double>(lo) && v->num <= static_cast<double>(hi))) return false;  // also rejects NaN
    const long long n = static_cast<long long>(v->num);
    if (static_cast<double>(n) != v->num) return false;
    out = static_cast<int>(n);
    return true;
}

bool parseHex(const std::string& s, uint64_t& out) {
    size_t i = 0;
    if (s.size() >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) i = 2;
    if (i >= s.size() || s.size() - i > 16) return false;
    uint64_t r = 0;
    for (; i < s.size(); ++i) {
        const char h = s[i];
        unsigned d;
        if (h >= '0' && h <= '9') d = static_cast<unsigned>(h - '0');
        else if (h >= 'a' && h <= 'f') d = static_cast<unsigned>(h - 'a' + 10);
        else if (h >= 'A' && h <= 'F') d = static_cast<unsigned>(h - 'A' + 10);
        else return false;
        r = (r << 4) | d;
    }
    out = r;
    return true;
}

}  // namespace ens::player
