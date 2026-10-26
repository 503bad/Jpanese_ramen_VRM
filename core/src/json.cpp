#include "rvrm/json.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace rvrm::json {

namespace {
const Value kNull;
const std::string kEmpty;

class Parser {
public:
    explicit Parser(std::string_view s) : s_(s) {}

    bool run(Value& out, std::string* err) {
        skipWs();
        if (!parseValue(out, 0)) {
            if (err) *err = error_ + " (offset " + std::to_string(pos_) + ")";
            return false;
        }
        skipWs();
        if (pos_ != s_.size()) {
            if (err) *err = "trailing characters (offset " + std::to_string(pos_) + ")";
            return false;
        }
        return true;
    }

private:
    std::string_view s_;
    size_t pos_ = 0;
    std::string error_;

    bool fail(const char* msg) { error_ = msg; return false; }
    void skipWs() {
        while (pos_ < s_.size() && (s_[pos_] == ' ' || s_[pos_] == '\t' || s_[pos_] == '\n' || s_[pos_] == '\r')) ++pos_;
    }
    bool consume(char c) {
        if (pos_ < s_.size() && s_[pos_] == c) { ++pos_; return true; }
        return false;
    }
    bool literal(const char* lit) {
        size_t n = std::strlen(lit);
        if (s_.substr(pos_, n) == lit) { pos_ += n; return true; }
        return false;
    }

    bool parseValue(Value& out, int depth) {
        if (depth > 256) return fail("nesting too deep");
        if (pos_ >= s_.size()) return fail("unexpected end");
        char c = s_[pos_];
        if (c == '{') return parseObject(out, depth);
        if (c == '[') return parseArray(out, depth);
        if (c == '"') {
            std::string str;
            if (!parseString(str)) return false;
            out = Value::makeString(std::move(str));
            return true;
        }
        if (literal("true")) { out = Value::makeBool(true); return true; }
        if (literal("false")) { out = Value::makeBool(false); return true; }
        if (literal("null")) { out = Value(); return true; }
        return parseNumber(out);
    }

    bool parseNumber(Value& out) {
        size_t start = pos_;
        if (consume('-')) {}
        bool digits = false;
        while (pos_ < s_.size() && s_[pos_] >= '0' && s_[pos_] <= '9') { ++pos_; digits = true; }
        if (consume('.')) {
            while (pos_ < s_.size() && s_[pos_] >= '0' && s_[pos_] <= '9') { ++pos_; digits = true; }
        }
        if (!digits) return fail("invalid value");
        if (pos_ < s_.size() && (s_[pos_] == 'e' || s_[pos_] == 'E')) {
            ++pos_;
            if (!consume('+')) consume('-');
            while (pos_ < s_.size() && s_[pos_] >= '0' && s_[pos_] <= '9') ++pos_;
        }
        std::string tmp(s_.substr(start, pos_ - start));
        out = Value::makeNumber(std::strtod(tmp.c_str(), nullptr));
        return true;
    }

    static void appendUtf8(std::string& o, uint32_t cp) {
        if (cp < 0x80) o += static_cast<char>(cp);
        else if (cp < 0x800) { o += static_cast<char>(0xC0 | (cp >> 6)); o += static_cast<char>(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) {
            o += static_cast<char>(0xE0 | (cp >> 12));
            o += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            o += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            o += static_cast<char>(0xF0 | (cp >> 18));
            o += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            o += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            o += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    bool parseHex4(uint32_t& v) {
        if (pos_ + 4 > s_.size()) return fail("bad \\u escape");
        v = 0;
        for (int i = 0; i < 4; ++i) {
            char c = s_[pos_++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= c - '0';
            else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
            else return fail("bad \\u escape");
        }
        return true;
    }

    bool parseString(std::string& out) {
        if (!consume('"')) return fail("expected string");
        while (pos_ < s_.size()) {
            char c = s_[pos_++];
            if (c == '"') return true;
            if (c != '\\') { out += c; continue; }
            if (pos_ >= s_.size()) break;
            char e = s_[pos_++];
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
                    uint32_t cp;
                    if (!parseHex4(cp)) return false;
                    if (cp >= 0xD800 && cp <= 0xDBFF && s_.substr(pos_, 2) == "\\u") {
                        pos_ += 2;
                        uint32_t lo;
                        if (!parseHex4(lo)) return false;
                        if (lo >= 0xDC00 && lo <= 0xDFFF) cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        else { appendUtf8(out, 0xFFFD); cp = lo; }
                    }
                    appendUtf8(out, cp);
                    break;
                }
                default: return fail("bad escape");
            }
        }
        return fail("unterminated string");
    }

    bool parseArray(Value& out, int depth) {
        consume('[');
        out = Value::makeArray();
        skipWs();
        if (consume(']')) return true;
        for (;;) {
            skipWs();
            Value v;
            if (!parseValue(v, depth + 1)) return false;
            out.items().push_back(std::move(v));
            skipWs();
            if (consume(']')) return true;
            if (!consume(',')) return fail("expected , or ]");
        }
    }

    bool parseObject(Value& out, int depth) {
        consume('{');
        out = Value::makeObject();
        skipWs();
        if (consume('}')) return true;
        for (;;) {
            skipWs();
            std::string key;
            if (!parseString(key)) return false;
            skipWs();
            if (!consume(':')) return fail("expected :");
            skipWs();
            Value v;
            if (!parseValue(v, depth + 1)) return false;
            out.members().emplace_back(std::move(key), std::move(v));
            skipWs();
            if (consume('}')) return true;
            if (!consume(',')) return fail("expected , or }");
        }
    }
};
}  // namespace

const std::string& Value::asString() const { return type_ == Type::String ? str_ : kEmpty; }

const Value& Value::operator[](size_t i) const {
    if (type_ != Type::Array || i >= arr_.size()) return kNull;
    return arr_[i];
}

const Value& Value::operator[](std::string_view key) const {
    if (type_ != Type::Object) return kNull;
    for (const auto& kv : obj_)
        if (kv.first == key) return kv.second;
    return kNull;
}

bool Value::has(std::string_view key) const {
    if (type_ != Type::Object) return false;
    for (const auto& kv : obj_)
        if (kv.first == key) return true;
    return false;
}

bool parse(std::string_view text, Value& out, std::string* error) {
    // UTF-8 BOM を許容
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF)
        text.remove_prefix(3);
    Parser p(text);
    return p.run(out, error);
}

}  // namespace rvrm::json
