#include "json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace cwut {
namespace {

class Parser {
public:
    Parser(const std::string& text, std::string* error) : t_(text), error_(error) {}

    bool run(JsonValue& out) {
        skipBom();
        skipSpace();
        if (!parseValue(out, 0)) return false;
        skipSpace();
        if (i_ != t_.size()) return fail("trailing data after top-level value");
        return true;
    }

private:
    const std::string& t_;
    size_t i_ = 0;
    std::string* error_;

    bool fail(const char* msg) {
        if (error_) {
            char buf[256];
            std::snprintf(buf, sizeof(buf), "%s at byte %zu", msg, i_);
            *error_ = buf;
        }
        return false;
    }

    void skipBom() {
        if (t_.size() >= 3 && static_cast<unsigned char>(t_[0]) == 0xEF &&
            static_cast<unsigned char>(t_[1]) == 0xBB &&
            static_cast<unsigned char>(t_[2]) == 0xBF) {
            i_ = 3;
        }
    }

    void skipSpace() {
        while (i_ < t_.size()) {
            char c = t_[i_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++i_;
            } else {
                break;
            }
        }
    }

    bool literal(const char* word) {
        size_t n = std::strlen(word);
        if (t_.compare(i_, n, word) != 0) return false;
        i_ += n;
        return true;
    }

    bool parseValue(JsonValue& out, int depth) {
        if (depth > kMaxJsonDepth) return fail("nesting too deep");
        if (i_ >= t_.size()) return fail("unexpected end of input");
        char c = t_[i_];
        switch (c) {
            case '{': return parseObject(out, depth);
            case '[': return parseArray(out, depth);
            case '"': {
                out.type = JsonValue::Type::String;
                return parseString(out.str);
            }
            case 't':
                if (!literal("true")) return fail("invalid literal");
                out.type = JsonValue::Type::Bool;
                out.boolean = true;
                return true;
            case 'f':
                if (!literal("false")) return fail("invalid literal");
                out.type = JsonValue::Type::Bool;
                out.boolean = false;
                return true;
            case 'n':
                if (!literal("null")) return fail("invalid literal");
                out.type = JsonValue::Type::Null;
                return true;
            default: return parseNumber(out);
        }
    }

    bool parseObject(JsonValue& out, int depth) {
        out.type = JsonValue::Type::Object;
        ++i_;  // '{'
        skipSpace();
        if (i_ < t_.size() && t_[i_] == '}') {
            ++i_;
            return true;
        }
        for (;;) {
            skipSpace();
            if (i_ >= t_.size() || t_[i_] != '"') return fail("expected object key");
            std::string key;
            if (!parseString(key)) return false;
            skipSpace();
            if (i_ >= t_.size() || t_[i_] != ':') return fail("expected ':'");
            ++i_;
            skipSpace();
            JsonValue child;
            if (!parseValue(child, depth + 1)) return false;
            out.object.emplace_back(key, std::move(child));
            skipSpace();
            if (i_ >= t_.size()) return fail("unterminated object");
            if (t_[i_] == ',') {
                ++i_;
                continue;
            }
            if (t_[i_] == '}') {
                ++i_;
                return true;
            }
            return fail("expected ',' or '}'");
        }
    }

    bool parseArray(JsonValue& out, int depth) {
        out.type = JsonValue::Type::Array;
        ++i_;  // '['
        skipSpace();
        if (i_ < t_.size() && t_[i_] == ']') {
            ++i_;
            return true;
        }
        for (;;) {
            skipSpace();
            JsonValue child;
            if (!parseValue(child, depth + 1)) return false;
            out.array.push_back(std::move(child));
            skipSpace();
            if (i_ >= t_.size()) return fail("unterminated array");
            if (t_[i_] == ',') {
                ++i_;
                continue;
            }
            if (t_[i_] == ']') {
                ++i_;
                return true;
            }
            return fail("expected ',' or ']'");
        }
    }

    static void appendUtf8(std::string& out, unsigned int cp) {
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }

    bool parseHex4(unsigned int& value) {
        if (i_ + 4 > t_.size()) return false;
        value = 0;
        for (int k = 0; k < 4; ++k) {
            char c = t_[i_ + k];
            unsigned int d;
            if (c >= '0' && c <= '9') {
                d = static_cast<unsigned int>(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                d = static_cast<unsigned int>(c - 'a' + 10);
            } else if (c >= 'A' && c <= 'F') {
                d = static_cast<unsigned int>(c - 'A' + 10);
            } else {
                return false;
            }
            value = (value << 4) | d;
        }
        i_ += 4;
        return true;
    }

    bool parseString(std::string& out) {
        ++i_;  // opening quote
        out.clear();
        for (;;) {
            if (i_ >= t_.size()) return fail("unterminated string");
            unsigned char c = static_cast<unsigned char>(t_[i_]);
            if (c == '"') {
                ++i_;
                return true;
            }
            if (c < 0x20) return fail("control character in string");
            if (c != '\\') {
                out.push_back(static_cast<char>(c));
                ++i_;
                continue;
            }
            ++i_;
            if (i_ >= t_.size()) return fail("unterminated escape");
            char e = t_[i_++];
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
                    unsigned int cp = 0;
                    if (!parseHex4(cp)) return fail("bad \\u escape");
                    if (cp >= 0xD800 && cp <= 0xDBFF) {
                        if (i_ + 1 < t_.size() && t_[i_] == '\\' && t_[i_ + 1] == 'u') {
                            size_t save = i_;
                            i_ += 2;
                            unsigned int low = 0;
                            if (parseHex4(low) && low >= 0xDC00 && low <= 0xDFFF) {
                                cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                            } else {
                                i_ = save;
                                cp = 0xFFFD;
                            }
                        } else {
                            cp = 0xFFFD;
                        }
                    } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                        cp = 0xFFFD;
                    }
                    appendUtf8(out, cp);
                    break;
                }
                default: return fail("unknown escape");
            }
        }
    }

    bool parseNumber(JsonValue& out) {
        size_t start = i_;
        if (i_ < t_.size() && t_[i_] == '-') ++i_;
        if (i_ >= t_.size()) return fail("bad number");
        if (t_[i_] == '0') {
            ++i_;
        } else if (t_[i_] >= '1' && t_[i_] <= '9') {
            while (i_ < t_.size() && t_[i_] >= '0' && t_[i_] <= '9') ++i_;
        } else {
            return fail("bad number");
        }
        if (i_ < t_.size() && t_[i_] == '.') {
            ++i_;
            if (i_ >= t_.size() || t_[i_] < '0' || t_[i_] > '9') return fail("bad fraction");
            while (i_ < t_.size() && t_[i_] >= '0' && t_[i_] <= '9') ++i_;
        }
        if (i_ < t_.size() && (t_[i_] == 'e' || t_[i_] == 'E')) {
            ++i_;
            if (i_ < t_.size() && (t_[i_] == '+' || t_[i_] == '-')) ++i_;
            if (i_ >= t_.size() || t_[i_] < '0' || t_[i_] > '9') return fail("bad exponent");
            while (i_ < t_.size() && t_[i_] >= '0' && t_[i_] <= '9') ++i_;
        }
        out.type = JsonValue::Type::Number;
        out.raw = t_.substr(start, i_ - start);
        out.number = std::strtod(out.raw.c_str(), nullptr);
        if (!std::isfinite(out.number)) return fail("number out of range");
        return true;
    }
};

void serialize(const JsonValue& v, std::string& out, int indent) {
    const std::string pad(static_cast<size_t>(indent) * 2, ' ');
    const std::string padInner(static_cast<size_t>(indent + 1) * 2, ' ');
    switch (v.type) {
        case JsonValue::Type::Null: out += "null"; break;
        case JsonValue::Type::Bool: out += v.boolean ? "true" : "false"; break;
        case JsonValue::Type::Number:
            if (!v.raw.empty()) {
                out += v.raw;
            } else {
                char buf[64];
                if (v.number == static_cast<double>(static_cast<long long>(v.number))) {
                    std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(v.number));
                } else {
                    std::snprintf(buf, sizeof(buf), "%.17g", v.number);
                }
                out += buf;
            }
            break;
        case JsonValue::Type::String:
            out += '"';
            out += JsonEscape(v.str);
            out += '"';
            break;
        case JsonValue::Type::Array:
            if (v.array.empty()) {
                out += "[]";
                break;
            }
            out += "[\n";
            for (size_t k = 0; k < v.array.size(); ++k) {
                out += padInner;
                serialize(v.array[k], out, indent + 1);
                if (k + 1 < v.array.size()) out += ',';
                out += '\n';
            }
            out += pad;
            out += ']';
            break;
        case JsonValue::Type::Object:
            if (v.object.empty()) {
                out += "{}";
                break;
            }
            out += "{\n";
            for (size_t k = 0; k < v.object.size(); ++k) {
                out += padInner;
                out += '"';
                out += JsonEscape(v.object[k].first);
                out += "\": ";
                serialize(v.object[k].second, out, indent + 1);
                if (k + 1 < v.object.size()) out += ',';
                out += '\n';
            }
            out += pad;
            out += '}';
            break;
    }
}

}  // namespace

const JsonValue* JsonValue::find(const std::string& key) const {
    if (type != Type::Object) return nullptr;
    for (const auto& kv : object) {
        if (kv.first == key) return &kv.second;
    }
    return nullptr;
}

JsonValue* JsonValue::find(const std::string& key) {
    if (type != Type::Object) return nullptr;
    for (auto& kv : object) {
        if (kv.first == key) return &kv.second;
    }
    return nullptr;
}

void JsonValue::set(const std::string& key, JsonValue value) {
    type = Type::Object;
    for (auto& kv : object) {
        if (kv.first == key) {
            kv.second = std::move(value);
            return;
        }
    }
    object.emplace_back(key, std::move(value));
}

bool JsonValue::erase(const std::string& key) {
    if (type != Type::Object) return false;
    for (size_t k = 0; k < object.size(); ++k) {
        if (object[k].first == key) {
            object.erase(object.begin() + static_cast<long long>(k));
            return true;
        }
    }
    return false;
}

JsonValue JsonValue::makeString(const std::string& s) {
    JsonValue v;
    v.type = Type::String;
    v.str = s;
    return v;
}

JsonValue JsonValue::makeNumber(double n) {
    JsonValue v;
    v.type = Type::Number;
    v.number = n;
    return v;
}

JsonValue JsonValue::makeObject() {
    JsonValue v;
    v.type = Type::Object;
    return v;
}

std::string JsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out.push_back(static_cast<char>(c));
                }
        }
    }
    return out;
}

bool JsonParse(const std::string& text, JsonValue& out, std::string* error) {
    out = JsonValue();
    if (text.size() > kMaxJsonBytes) {
        if (error) *error = "input too large";
        return false;
    }
    Parser p(text, error);
    return p.run(out);
}

std::string JsonSerialize(const JsonValue& value) {
    std::string out;
    serialize(value, out, 0);
    out += '\n';
    return out;
}

}  // namespace cwut
