// A small JSON reader and writer: enough for Outputs, manifests and settings. Never throws; bad
// input gives std::nullopt. Framework-free: the core tests run without Fcitx5 or IBus.
#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace bl::json {

struct Value {
    enum class Type { kNull, kBool, kNumber, kString, kArray, kObject } type = Type::kNull;
    bool boolean = false;
    double number = 0;
    std::string string;  // UTF-8
    std::vector<Value> array;
    std::map<std::string, Value> object;

    // The member `key` of an object, or nullptr.
    const Value* Get(const std::string& key) const;
    const std::string* String() const { return type == Type::kString ? &string : nullptr; }
    bool IsObject() const { return type == Type::kObject; }
    bool IsArray() const { return type == Type::kArray; }
    bool IsNull() const { return type == Type::kNull; }

    // Member helpers with a fallback for missing or mistyped members.
    std::string GetString(const std::string& key, std::string fallback = {}) const;
    bool GetBool(const std::string& key, bool fallback = false) const;
    double GetNumber(const std::string& key, double fallback = 0) const;
};

std::optional<Value> Parse(std::string_view text);

// Serializes `value` compactly (object keys in sorted order).
std::string Write(const Value& value);
// `text` as a JSON string literal, quotes included.
std::string Quote(std::string_view text);

// `over`'s top-level members replace `base`'s (superkeyboard's rule for a manifest's settings and a
// session's own). Either may be missing or not an object.
Value MergeTop(const Value* base, const Value* over);

}  // namespace bl::json
