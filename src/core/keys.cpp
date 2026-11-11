#include "keys.h"

namespace bl {
namespace {

// evdev code → KeyboardEvent.code for keys whose position matters: the JIS kana space and the few
// others that type. nullptr: not a character key.
struct CodeTable {
    const char* codes[0x80] = {};
    constexpr CodeTable() {
        const char* digits[] = {"Digit1", "Digit2", "Digit3", "Digit4", "Digit5",
                                "Digit6", "Digit7", "Digit8", "Digit9", "Digit0"};
        for (int i = 0; i < 10; ++i) codes[2 + i] = digits[i];
        codes[12] = "Minus";
        codes[13] = "Equal";  // JIS ^
        const char* q_row[] = {"KeyQ", "KeyW", "KeyE", "KeyR", "KeyT", "KeyY",
                               "KeyU", "KeyI", "KeyO", "KeyP", "BracketLeft", "BracketRight"};
        for (int i = 0; i < 12; ++i) codes[16 + i] = q_row[i];
        const char* a_row[] = {"KeyA", "KeyS", "KeyD", "KeyF", "KeyG", "KeyH",
                               "KeyJ", "KeyK", "KeyL", "Semicolon", "Quote"};
        for (int i = 0; i < 11; ++i) codes[30 + i] = a_row[i];
        codes[41] = "Backquote";  // 半角/全角 on JIS (KEY_GRAVE)
        codes[43] = "Backslash";  // JIS ] (USB's Non-US #)
        const char* z_row[] = {"KeyZ", "KeyX", "KeyC", "KeyV", "KeyB",
                               "KeyN", "KeyM", "Comma", "Period", "Slash"};
        for (int i = 0; i < 10; ++i) codes[44 + i] = z_row[i];
        codes[85] = "Backquote";      // KEY_ZENKAKUHANKAKU, from keyboards that send it
        codes[86] = "IntlBackslash";  // ISO only
        codes[89] = "IntlRo";         // ろ
        codes[124] = "IntlYen";       // ¥
    }
};
constexpr CodeTable kCodes;

// JIS legends (macOS ABC on a JIS keyboard): code, unshifted, shifted. Letters are separate.
struct Legend {
    const char* code;
    const char* plain;
    const char* shifted;
};
constexpr Legend kJis[] = {
    {"Digit1", "1", "!"},  {"Digit2", "2", "\""}, {"Digit3", "3", "#"},  {"Digit4", "4", "$"},
    {"Digit5", "5", "%"},  {"Digit6", "6", "&"},  {"Digit7", "7", "'"},  {"Digit8", "8", "("},
    {"Digit9", "9", ")"},  {"Digit0", "0", "0"},  {"Minus", "-", "="},   {"Equal", "^", "~"},
    {"IntlYen", "\xC2\xA5", "|"},  // ¥
    {"BracketLeft", "@", "`"},     {"BracketRight", "[", "{"}, {"Semicolon", ";", "+"},
    {"Quote", ":", "*"},           {"Backslash", "]", "}"},    {"Comma", ",", "<"},
    {"Period", ".", ">"},          {"Slash", "/", "?"},        {"IntlRo", "\\", "_"},
};

const char* Named(unsigned code) {
    switch (code) {
        case ev::kBackspace: return "Backspace";
        case ev::kTab: return "Tab";
        case ev::kEnter: case ev::kKpEnter: return "Enter";  // the keypad's too, as on macOS
        case ev::kEsc: return "Escape";
        case ev::kSpace: return "Space";
        case ev::kLeft: return "ArrowLeft";
        case ev::kUp: return "ArrowUp";
        case ev::kRight: return "ArrowRight";
        case ev::kDown: return "ArrowDown";
        case ev::kHome: return "Home";
        case ev::kEnd: return "End";
        case ev::kPageUp: return "PageUp";
        case ev::kPageDown: return "PageDown";
        case ev::kInsert: return "Insert";
        case ev::kDelete: return "Delete";
        case ev::kPause: return "Pause";
        case ev::kCompose: return "ContextMenu";
        case ev::kKpAsterisk: return "NumpadMultiply";
        case 78: return "NumpadAdd";
        case 74: return "NumpadSubtract";
        case 83: return "NumpadDecimal";
        case 98: return "NumpadDivide";
        case 95: case 121: return "NumpadComma";
        case 82: return "Numpad0";
        case 79: return "Numpad1";
        case 80: return "Numpad2";
        case 81: return "Numpad3";
        case 75: return "Numpad4";
        case 76: return "Numpad5";
        case 77: return "Numpad6";
        case 71: return "Numpad7";
        case 72: return "Numpad8";
        case 73: return "Numpad9";
    }
    return nullptr;
}

}  // namespace

bool IsModifier(unsigned code) {
    switch (code) {
        case ev::kLeftShift: case ev::kRightShift:
        case ev::kLeftCtrl: case ev::kRightCtrl:
        case ev::kLeftAlt: case ev::kRightAlt:
        case ev::kLeftMeta: case ev::kRightMeta:
        case ev::kCapsLock: case ev::kNumLock: case ev::kScrollLock:
            return true;
    }
    return false;
}

LangKey LangKeyOf(const KeyDown& key) {
    switch (key.code) {
        case ev::kMuhenkan: return LangKey::kMuhenkan;
        case ev::kHenkan: return LangKey::kHenkan;
        case ev::kKatakanaHiragana: case ev::kKatakana: case ev::kHiragana: return LangKey::kKatakana;
        // An Apple JIS keyboard's 英数 and かな are HID LANG2 / LANG1, which Linux names after the
        // Korean keys (docs/spec.md §4: [measure] on a real keyboard).
        case ev::kHanja: return LangKey::kEisu;
        case ev::kHangeul: return LangKey::kKana;
        case ev::kCapsLock: return LangKey::kCapsLock;
    }
    if (key.code) return LangKey::kNone;
    switch (key.sym) {
        case sym::kMuhenkan: return LangKey::kMuhenkan;
        case sym::kHenkan: return LangKey::kHenkan;
        case sym::kHiraganaKatakana: case sym::kHiragana: case sym::kKatakana: return LangKey::kKatakana;
        case sym::kHangulHanja: return LangKey::kEisu;
        case sym::kHangul: return LangKey::kKana;
        case sym::kEisuToggle: case sym::kCapsLock: return LangKey::kCapsLock;
    }
    return LangKey::kNone;
}

std::string CodeOf(const KeyDown& key) {
    if (IsModifier(key.code)) return "";
    if (key.code >= ev::kF1 && key.code <= ev::kF10) return "F" + std::to_string(key.code - ev::kF1 + 1);
    if (key.code == ev::kF11) return "F11";
    if (key.code == ev::kF12) return "F12";
    if (key.code >= ev::kF13 && key.code <= ev::kF24) return "F" + std::to_string(key.code - ev::kF13 + 13);
    if (const char* named = Named(key.code)) return named;
    if (key.code < 0x80 && kCodes.codes[key.code]) return kCodes.codes[key.code];
    return "Unidentified";
}

std::string JisCharacter(const std::string& code, bool shift, bool caps) {
    if (code.size() == 4 && code.compare(0, 3, "Key") == 0 && code[3] >= 'A' && code[3] <= 'Z') {
        const bool upper = shift || caps;  // bada-compatible §7.3: Caps Lock types capitals, ⇧ or not
        return std::string(1, upper ? code[3] : static_cast<char>(code[3] - 'A' + 'a'));
    }
    for (const Legend& l : kJis) {
        if (code == l.code) return shift ? l.shifted : l.plain;
    }
    return {};
}

}  // namespace bl
