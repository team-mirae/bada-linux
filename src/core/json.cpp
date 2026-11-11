#include "json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace bl::json {
namespace {

class Parser {
public:
    explicit Parser(std::string_view text) : s_(text) {}

    std::optional<Value> Document() {
        Value v;
        if (!Parse(&v, 0)) return std::nullopt;
        Space();
        if (i_ != s_.size()) return std::nullopt;
        return v;
    }

private:
    static constexpr int kMaxDepth = 64;

    void Space() {
        while (i_ < s_.size() && (s_[i_] == ' ' || s_[i_] == '\t' || s_[i_] == '\n' || s_[i_] == '\r')) ++i_;
    }

    bool Literal(std::string_view word) {
        if (s_.substr(i_, word.size()) != word) return false;
        i_ += word.size();
        return true;
    }

    bool Parse(Value* v, int depth) {
        if (depth > kMaxDepth) return false;
        Space();
        if (i_ >= s_.size()) return false;
        const char c = s_[i_];
        if (c == '{') return Object(v, depth);
        if (c == '[') return Array(v, depth);
        if (c == '"') {
            v->type = Value::Type::kString;
            return String(&v->string);
        }
        if (Literal("true")) {
            v->type = Value::Type::kBool;
            v->boolean = true;
            return true;
        }
        if (Literal("false")) {
            v->type = Value::Type::kBool;
            v->boolean = false;
            return true;
        }
        if (Literal("null")) {
            v->type = Value::Type::kNull;
            return true;
        }
        return Number(v);
    }

    bool Number(Value* v) {
        const size_t start = i_;
        if (i_ < s_.size() && s_[i_] == '-') ++i_;
        bool digits = false;
        while (i_ < s_.size() && ((s_[i_] >= '0' && s_[i_] <= '9') || s_[i_] == '.' || s_[i_] == 'e' ||
                                  s_[i_] == 'E' || s_[i_] == '+' || s_[i_] == '-')) {
            digits = true;
            ++i_;
        }
        if (!digits) return false;
        const std::string text(s_.substr(start, i_ - start));
        char* end = nullptr;
        const double d = std::strtod(text.c_str(), &end);
        if (!end || *end) return false;
        v->type = Value::Type::kNumber;
        v->number = d;
        return true;
    }

    static void Utf8(std::string* out, unsigned cp) {
        if (cp < 0x80) {
            out->push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out->push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out->push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out->push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out->push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out->push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out->push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }

    bool Hex4(unsigned* out) {
        if (i_ + 4 > s_.size()) return false;
        unsigned v = 0;
        for (int k = 0; k < 4; ++k) {
            const char c = s_[i_++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') v |= static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= static_cast<unsigned>(c - 'A' + 10);
            else return false;
        }
        *out = v;
        return true;
    }

    bool String(std::string* out) {
        ++i_;  // the opening quote
        while (i_ < s_.size()) {
            const char c = s_[i_++];
            if (c == '"') return true;
            if (static_cast<unsigned char>(c) < 0x20) return false;
            if (c != '\\') {
                out->push_back(c);
                continue;
            }
            if (i_ >= s_.size()) return false;
            switch (s_[i_++]) {
                case '"': out->push_back('"'); break;
                case '\\': out->push_back('\\'); break;
                case '/': out->push_back('/'); break;
                case 'b': out->push_back('\b'); break;
                case 'f': out->push_back('\f'); break;
                case 'n': out->push_back('\n'); break;
                case 'r': out->push_back('\r'); break;
                case 't': out->push_back('\t'); break;
                case 'u': {
                    unsigned cp = 0;
                    if (!Hex4(&cp)) return false;
                    if (cp >= 0xD800 && cp < 0xDC00 && s_.substr(i_, 2) == "\\u") {
                        i_ += 2;
                        unsigned low = 0;
                        if (!Hex4(&low) || low < 0xDC00 || low >= 0xE000) return false;
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                    } else if (cp >= 0xD800 && cp < 0xE000) {
                        cp = 0xFFFD;  // a lone surrogate
                    }
                    Utf8(out, cp);
                    break;
                }
                default:
                    return false;
            }
        }
        return false;
    }

    bool Array(Value* v, int depth) {
        ++i_;
        v->type = Value::Type::kArray;
        Space();
        if (i_ < s_.size() && s_[i_] == ']') {
            ++i_;
            return true;
        }
        while (true) {
            Value item;
            if (!Parse(&item, depth + 1)) return false;
            v->array.push_back(std::move(item));
            Space();
            if (i_ >= s_.size()) return false;
            if (s_[i_] == ',') {
                ++i_;
                continue;
            }
            if (s_[i_] == ']') {
                ++i_;
                return true;
            }
            return false;
        }
    }

    bool Object(Value* v, int depth) {
        ++i_;
        v->type = Value::Type::kObject;
        Space();
        if (i_ < s_.size() && s_[i_] == '}') {
            ++i_;
            return true;
        }
        while (true) {
            Space();
            if (i_ >= s_.size() || s_[i_] != '"') return false;
            std::string key;
            if (!String(&key)) return false;
            Space();
            if (i_ >= s_.size() || s_[i_] != ':') return false;
            ++i_;
            Value item;
            if (!Parse(&item, depth + 1)) return false;
            v->object[std::move(key)] = std::move(item);
            Space();
            if (i_ >= s_.size()) return false;
            if (s_[i_] == ',') {
                ++i_;
                continue;
            }
            if (s_[i_] == '}') {
                ++i_;
                return true;
            }
            return false;
        }
    }

    std::string_view s_;
    size_t i_ = 0;
};

void WriteTo(const Value& v, std::string* out) {
    switch (v.type) {
        case Value::Type::kNull: *out += "null"; break;
        case Value::Type::kBool: *out += v.boolean ? "true" : "false"; break;
        case Value::Type::kNumber: {
            if (!std::isfinite(v.number)) {
                *out += "null";
            } else if (v.number == std::floor(v.number) && std::fabs(v.number) < 1e15) {
                *out += std::to_string(static_cast<long long>(v.number));
            } else {
                char buffer[32];
                std::snprintf(buffer, sizeof buffer, "%.17g", v.number);
                *out += buffer;
            }
            break;
        }
        case Value::Type::kString: *out += Quote(v.string); break;
        case Value::Type::kArray: {
            *out += '[';
            for (size_t i = 0; i < v.array.size(); ++i) {
                if (i) *out += ',';
                WriteTo(v.array[i], out);
            }
            *out += ']';
            break;
        }
        case Value::Type::kObject: {
            *out += '{';
            bool first = true;
            for (const auto& [key, item] : v.object) {
                if (!first) *out += ',';
                first = false;
                *out += Quote(key);
                *out += ':';
                WriteTo(item, out);
            }
            *out += '}';
            break;
        }
    }
}

}  // namespace

const Value* Value::Get(const std::string& key) const {
    if (type != Type::kObject) return nullptr;
    const auto it = object.find(key);
    return it == object.end() ? nullptr : &it->second;
}

std::string Value::GetString(const std::string& key, std::string fallback) const {
    const Value* v = Get(key);
    return v && v->type == Type::kString ? v->string : fallback;
}

bool Value::GetBool(const std::string& key, bool fallback) const {
    const Value* v = Get(key);
    return v && v->type == Type::kBool ? v->boolean : fallback;
}

double Value::GetNumber(const std::string& key, double fallback) const {
    const Value* v = Get(key);
    return v && v->type == Type::kNumber ? v->number : fallback;
}

std::optional<Value> Parse(std::string_view text) { return Parser(text).Document(); }

std::string Write(const Value& value) {
    std::string out;
    WriteTo(value, &out);
    return out;
}

std::string Quote(std::string_view text) {
    std::string out = "\"";
    for (const char c : text) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buffer[8];
                    std::snprintf(buffer, sizeof buffer, "\\u%04x", static_cast<unsigned>(c));
                    out += buffer;
                } else {
                    out += c;
                }
        }
    }
    out += '"';
    return out;
}

Value MergeTop(const Value* base, const Value* over) {
    Value merged;
    merged.type = Value::Type::kObject;
    for (const Value* v : {base, over}) {
        if (!v || !v->IsObject()) continue;
        for (const auto& [key, item] : v->object) merged.object[key] = item;
    }
    return merged;
}

}  // namespace bl::json
