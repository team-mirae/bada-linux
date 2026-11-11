// Linux key events → bada-compatible key events (docs/spec.md §4). Framework-free, so it's
// unit-tested without Fcitx5 or IBus.
//
// Keys go by **evdev code** (linux/input-event-codes.h), the physical position, which is what
// KeyboardEvent.code names. Fcitx5 reports the XKB keycode (evdev + 8), IBus the evdev code: the
// frontends convert. The keysym (what the layout typed) is used only where the position doesn't
// say enough: the JIS layout's Caps Lock key, and synthetic keys that come without a code.
#pragma once

#include <string>

namespace bl {

// evdev codes used by name.
namespace ev {
inline constexpr unsigned kEsc = 1, kBackspace = 14, kTab = 15, kEnter = 28, kLeftCtrl = 29,
                          kGrave = 41, kLeftShift = 42, kRightShift = 54, kKpAsterisk = 55,
                          kLeftAlt = 56, kSpace = 57, kCapsLock = 58, kF1 = 59, kF10 = 68,
                          kNumLock = 69, kScrollLock = 70, kZenkakuHankaku = 85, kF11 = 87,
                          kF12 = 88, kRo = 89, kKatakana = 90, kHiragana = 91, kHenkan = 92,
                          kKatakanaHiragana = 93, kMuhenkan = 94, kKpEnter = 96, kRightCtrl = 97,
                          kRightAlt = 100, kHome = 102, kUp = 103, kPageUp = 104, kLeft = 105,
                          kRight = 106, kEnd = 107, kDown = 108, kPageDown = 109, kInsert = 110,
                          kDelete = 111, kPause = 119, kHangeul = 122, kHanja = 123, kYen = 124,
                          kLeftMeta = 125, kRightMeta = 126, kCompose = 127, kF13 = 183, kF24 = 194;
}  // namespace ev

// Keysyms used here (X11 keysymdef.h).
namespace sym {
inline constexpr unsigned kMuhenkan = 0xff22, kHenkan = 0xff23, kHiragana = 0xff25,
                          kKatakana = 0xff26, kHiraganaKatakana = 0xff27, kEisuToggle = 0xff30,
                          kHangul = 0xff31, kHangulHanja = 0xff34, kCapsLock = 0xffe5;
}  // namespace sym

struct Mods {
    bool shift = false;
    bool ctrl = false;
    bool alt = false;
    bool meta = false;  // Super
    bool Plain() const { return !ctrl && !alt && !meta; }
};

// A key press as the frontend sees it.
struct KeyDown {
    unsigned code = 0;  // evdev code; 0: none (a synthetic key)
    unsigned sym = 0;   // the keysym; 0: unknown
    bool repeat = false;  // an auto-repeat (the frontend saw no release since the last press)
    Mods mods;
    bool caps = false;  // Caps Lock is on
};

// The keys the host handles itself (bada-compatible §7.4, adapted: docs/spec.md §4). They never
// reach the engine as key events, except as "Lang1" (next entry).
enum class LangKey {
    kNone,
    kMuhenkan,  // 無変換: muhenkan mode
    kHenkan,    // 変換: henkan mode, profile "henkan"
    kKatakana,  // カタカナ/ひらがな: henkan mode, profile "katakana"
    kEisu,      // 英数 (an Apple JIS keyboard's): muhenkan
    kKana,      // かな (an Apple JIS keyboard's): the mac-kana profile, if bound (docs/spec.md §4.2)
    kCapsLock,  // the Caps Lock position (docs/spec.md §5)
};

// The language key a press is, if any. evdev codes first; keysyms for keys without a code.
LangKey LangKeyOf(const KeyDown& key);

// Whether `code` is a modifier on its own (Shift, Ctrl, Alt, Super, Caps Lock, Num Lock...).
bool IsModifier(unsigned code);

// The KeyboardEvent.code the engine gets; "" for a modifier on its own, which never goes to the
// engine: it belongs to the next key and must never commit. A key without a name is
// "Unidentified": the engine commits what's staged and passes it on.
std::string CodeOf(const KeyDown& key);

// The character a plain JIS text field types for `code` (macOS ABC on a JIS keyboard, as
// bada-compatible's host and superkeyboard do), as UTF-8; "" when the key types none. With Caps Lock
// on, letters are capitals, ⇧ or not (bada-compatible v1.1 §7.3).
std::string JisCharacter(const std::string& code, bool shift, bool caps);

}  // namespace bl
