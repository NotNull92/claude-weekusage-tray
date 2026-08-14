// Minimal JSON reader/writer for ClaudeWeekUsageTray.
//
// Scope note: this exists only to read the status-line payload Claude Code
// writes to stdin and to edit ~/.claude/settings.json without disturbing keys
// the app does not own. It is deliberately small and fails closed.
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace cwut {

struct JsonValue {
    enum class Type { Null, Bool, Number, String, Array, Object };

    Type type = Type::Null;
    bool boolean = false;
    double number = 0.0;
    // Original number text, kept so settings.json round-trips without the
    // value drifting through a double.
    std::string raw;
    std::string str;
    std::vector<JsonValue> array;
    std::vector<std::pair<std::string, JsonValue>> object;

    bool isNull() const { return type == Type::Null; }
    bool isBool() const { return type == Type::Bool; }
    bool isNumber() const { return type == Type::Number; }
    bool isString() const { return type == Type::String; }
    bool isArray() const { return type == Type::Array; }
    bool isObject() const { return type == Type::Object; }

    // Returns nullptr when this is not an object or the key is absent.
    const JsonValue* find(const std::string& key) const;
    JsonValue* find(const std::string& key);

    // Inserts or replaces a key, preserving the position of existing keys.
    void set(const std::string& key, JsonValue value);
    bool erase(const std::string& key);

    static JsonValue makeString(const std::string& s);
    static JsonValue makeNumber(double v);
    static JsonValue makeObject();
};

// Parses UTF-8 JSON text. Returns false and fills `error` on any problem.
// Input larger than kMaxJsonBytes or nested deeper than kMaxJsonDepth is
// rejected rather than parsed.
bool JsonParse(const std::string& text, JsonValue& out, std::string* error);

// Serializes with two-space indentation and a trailing newline.
std::string JsonSerialize(const JsonValue& value);

std::string JsonEscape(const std::string& s);

constexpr size_t kMaxJsonBytes = 4u * 1024u * 1024u;
constexpr int kMaxJsonDepth = 64;

}  // namespace cwut
