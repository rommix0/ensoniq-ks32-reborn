#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace ens::player {

// Minimal JSON document tree. Objects keep file order; duplicate keys are kept (find() returns the first).
struct JsonValue {
    enum class Type { Null, Bool, Number, String, Array, Object } type = Type::Null;
    bool b = false;
    double num = 0;
    std::string str;
    std::vector<JsonValue> arr;
    std::vector<std::pair<std::string, JsonValue>> obj;

    // The member named `key`, or nullptr if absent or this is not an object.
    const JsonValue* find(const std::string& key) const;
};

// Recursive-descent parser (nesting limited to 64). \uXXXX is decoded for the BMP only (to UTF-8; a
// surrogate becomes U+FFFD). Never throws; on failure returns false with a non-empty err.
bool parseJson(const std::string& text, JsonValue& out, std::string& err);

// Helpers for the data readers. All are total functions (return false instead of throwing).
// v is an integral Number within [lo, hi]:
bool jsonInt(const JsonValue* v, long long lo, long long hi, int& out);
// "0x1b" / "1B" / "FFFFFFFF9040": 1..16 hex digits, optional 0x prefix:
bool parseHex(const std::string& s, uint64_t& out);

}  // namespace ens::player
