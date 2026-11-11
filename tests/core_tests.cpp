// Core tests: JSON, Outputs, the key map, manifests, settings, the publisher, and the host and
// session logic typed through a plain text field into a real bada-compatible engine (shout, from
// team-mirae/bada-compatible). Framework-free: they run anywhere (tools/test.sh runs them in Docker).
//
//   bl_core_tests [path/to/libshout.so]   (default: $SHOUT_LIB; without one, engine tests skip)
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "engine.h"
#include "host.h"
#include "json.h"
#include "keys.h"
#include "manifest.h"
#include "output.h"
#include "publish.h"
#include "session.h"
#include "settings.h"

namespace fs = std::filesystem;
using namespace bl;

namespace {

int failures = 0;
int checks = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        ++checks;                                                            \
        if (!(cond)) {                                                       \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
            ++failures;                                                      \
        }                                                                    \
    } while (0)

#define CHECK_EQ(a, b)                                                                     \
    do {                                                                                   \
        ++checks;                                                                          \
        const auto& x_ = (a);                                                              \
        const auto& y_ = (b);                                                              \
        if (!(x_ == y_)) {                                                                 \
            std::ostringstream s_;                                                         \
            s_ << x_ << " != " << y_;                                                      \
            std::printf("FAIL %s:%d: %s == %s (%s)\n", __FILE__, __LINE__, #a, #b,         \
                        s_.str().c_str());                                                 \
            ++failures;                                                                    \
        }                                                                                  \
    } while (0)

// --- a text field (bada-compatible §7.1, §7.3), in code points

std::u32string U32(const std::string& s) {
    std::u32string out;
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        int n = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
        char32_t cp = n == 1 ? c : n == 2 ? (c & 0x1F) : n == 3 ? (c & 0x0F) : (c & 0x07);
        for (int k = 1; k < n && i + k < s.size(); ++k) cp = (cp << 6) | (s[i + k] & 0x3F);
        out += cp;
        i += n;
    }
    return out;
}

std::string U8(const std::u32string& s) {
    std::string out;
    for (char32_t cp : s) {
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    return out;
}

class TextField final : public Field {
public:
    void Apply(const Output& out) override {
        if (!out.commit.empty()) {
            size_t s = sel_start_, e = sel_end_;
            if (marked_len_ && !out.replace_selection) {
                s = marked_start_;
                e = marked_start_ + marked_len_;
            }
            Replace(s, e, U32(out.commit));
            marked_len_ = 0;
        }
        if (!out.preedit.empty()) {
            size_t s = sel_start_, e = sel_end_;
            if (marked_len_) {
                s = marked_start_;
                e = marked_start_ + marked_len_;
            }
            const std::u32string p = U32(out.preedit);
            Replace(s, e, p);
            marked_start_ = s;
            marked_len_ = p.size();
        } else if (marked_len_) {
            Replace(marked_start_, marked_start_ + marked_len_, U"");
            marked_len_ = 0;
        }
        menu = out.menu;
    }
    std::string Selection() override { return U8(text_.substr(sel_start_, sel_end_ - sel_start_)); }
    void Type(const std::string& text) override {
        Replace(sel_start_, sel_end_, U32(text));
        ++typed;
    }

    // What the app does with a key the IME didn't eat (plain field, §7.3).
    void App(const std::string& code, const KeyDown& key) {
        if (!key.mods.Plain()) return;
        if (code == "Space") Replace(sel_start_, sel_end_, U" ");
        else if (code == "Enter") Replace(sel_start_, sel_end_, U"\n");
        else if (code == "Tab") Replace(sel_start_, sel_end_, U"\t");
        else if (code == "Backspace" && sel_start_ != sel_end_) Replace(sel_start_, sel_end_, U"");
        else if (code == "Backspace" && sel_start_ > 0) Replace(sel_start_ - 1, sel_start_, U"");
        else if (const std::string c = JisCharacter(code, key.mods.shift, key.caps); !c.empty()) {
            Replace(sel_start_, sel_end_, U32(c));  // the system's layout, taken as JIS here
            ++app_typed;
        }
    }

    void Select(size_t s, size_t e) {
        sel_start_ = s;
        sel_end_ = e;
    }
    std::string text() const { return U8(text_); }
    std::string preedit() const { return U8(text_.substr(marked_start_, marked_len_)); }
    std::vector<std::string> menu;
    int typed = 0;
    int app_typed = 0;

private:
    void Replace(size_t s, size_t e, const std::u32string& with) {
        text_.replace(s, e - s, with);
        sel_start_ = sel_end_ = s + with.size();
    }
    std::u32string text_;
    size_t sel_start_ = 0, sel_end_ = 0, marked_start_ = 0, marked_len_ = 0;
};

// Key tokens: a KeyboardEvent.code (S-, C-, A-, M- prefixes), or a language key by name, as evdev
// codes and the keysyms the XKB jp layout gives them.
struct Named {
    unsigned code;
    unsigned sym;
};
const std::map<std::string, Named>& Keys() {
    static const std::map<std::string, Named> keys = [] {
        std::map<std::string, Named> k;
        const char* q = "QWERTYUIOP";
        for (unsigned i = 0; i < 10; ++i) k[std::string("Key") + q[i]] = {16 + i, static_cast<unsigned>(q[i] + 32)};
        const char* a = "ASDFGHJKL";
        for (unsigned i = 0; i < 9; ++i) k[std::string("Key") + a[i]] = {30 + i, static_cast<unsigned>(a[i] + 32)};
        const char* z = "ZXCVBNM";
        for (unsigned i = 0; i < 7; ++i) k[std::string("Key") + z[i]] = {44 + i, static_cast<unsigned>(z[i] + 32)};
        for (unsigned i = 0; i < 9; ++i) k["Digit" + std::to_string(i + 1)] = {2 + i, '1' + i};
        k["Digit0"] = {11, '0'};
        k["Period"] = {52, '.'};
        k["Comma"] = {51, ','};
        k["BracketLeft"] = {26, '@'};
        k["Space"] = {57, ' '};
        k["Enter"] = {28, 0xff0d};
        k["Tab"] = {15, 0xff09};
        k["Backspace"] = {14, 0xff08};
        k["Escape"] = {1, 0xff1b};
        k["ArrowUp"] = {103, 0xff52};
        k["ArrowDown"] = {108, 0xff54};
        k["zenkaku"] = {41, 0xff2a};
        k["muhenkan"] = {94, sym::kMuhenkan};
        k["henkan"] = {92, sym::kHenkan};
        k["katakana"] = {93, sym::kHiraganaKatakana};
        k["eisu"] = {123, sym::kHangulHanja};
        k["kana"] = {122, sym::kHangul};
        k["caps"] = {58, sym::kCapsLock};          // Caps Lock under a us layout, or ⇧ under jp
        k["eisu-caps"] = {58, sym::kEisuToggle};   // the jp layout's unshifted Caps Lock key
        k["Shift"] = {42, 0xffe1};
        k["ralt"] = {100, 0xffea};
        return k;
    }();
    return keys;
}

KeyDown Parse(std::string token) {
    KeyDown k;
    while (token.size() > 2 && token[1] == '-') {
        if (token[0] == 'S') k.mods.shift = true;
        if (token[0] == 'C') k.mods.ctrl = true;
        if (token[0] == 'A') k.mods.alt = true;
        if (token[0] == 'M') k.mods.meta = true;
        token = token.substr(2);
    }
    const auto it = Keys().find(token);
    if (it == Keys().end()) {
        std::printf("FAIL unknown test key %s\n", token.c_str());
        ++failures;
        return k;
    }
    k.code = it->second.code;
    k.sym = it->second.sym;
    return k;
}

struct Typist {
    Host& host;
    TextField& field;
    bool caps = false;  // Caps Lock, as the system holds it

    void Type(const std::string& tokens) {
        std::istringstream in(tokens);
        std::string token;
        while (in >> token) Press(token);
    }

    bool Press(const std::string& token) {
        KeyDown k = Parse(token);
        // XKB changes the lock on a Caps_Lock press before the IME sees it.
        if (k.sym == sym::kCapsLock) caps = !caps;
        k.caps = caps;
        const bool eaten = host.Key(k, field);
        if (const auto wanted = host.TakeCapsWanted()) caps = *wanted;
        if (!eaten) field.App(CodeOf(k), k);
        return eaten;
    }

    // A press and its release, nothing between (a tap).
    void Tap(const std::string& token) {
        Press(token);
        KeyDown k = Parse(token);
        host.KeyUp(k, field);
    }
};

// --- tests

void TestJson() {
    auto v = json::Parse(R"({"a": [1, 2.5, true, null, "x\u00e9\ud83d\ude00\n"], "b": {"c": -3e2}})");
    CHECK(v.has_value());
    CHECK_EQ(v->Get("a")->array.size(), size_t{5});
    CHECK_EQ(*v->Get("a")->array[4].String(), std::string("x\xC3\xA9\xF0\x9F\x98\x80\n"));
    CHECK_EQ(json::Write(*v), std::string(R"({"a":[1,2.5,true,null,"x)") + "\xC3\xA9\xF0\x9F\x98\x80" +
                                  R"(\n"],"b":{"c":-300}})");
    for (const char* bad : {"", "{", "[1,]", "{\"a\" 1}", "nul", "\"\\x\"", "{} x", "[\"\x01\"]"}) {
        CHECK(!json::Parse(bad).has_value());
    }
    const auto base = json::Parse(R"({"forms": ["upper"], "keep": 1})");
    const auto over = json::Parse(R"({"forms": ["lower"]})");
    CHECK_EQ(json::Write(json::MergeTop(&*base, &*over)), std::string(R"({"forms":["lower"],"keep":1})"));
}

void TestOutput() {
    const Output o = ParseOutput(R"({"commit":"A","replace_selection":true,"preedit":"B","menu":["B","b"],
        "selected":1,"zero":1,"consumed":true,"horizontal":true,"notes":["n",""],"japanese":[false,true],
        "need_selection":"shout","future":{"x":1}})");
    CHECK_EQ(o.commit, std::string("A"));
    CHECK(o.replace_selection && o.consumed && o.horizontal);
    CHECK_EQ(o.zero, size_t{1});
    CHECK(o.need_selection && *o.need_selection == "shout");
    const Output bad = ParseOutput("not json");
    CHECK(!bad.consumed && bad.commit.empty() && !bad.need_selection);
    CHECK_EQ(ParseOutput(R"({"selected":-4})").selected, size_t{0});
}

void TestKeys() {
    auto code = [](unsigned c, unsigned s = 0) {
        KeyDown k;
        k.code = c;
        k.sym = s;
        return CodeOf(k);
    };
    auto lang = [](unsigned c, unsigned s = 0) {
        KeyDown k;
        k.code = c;
        k.sym = s;
        return LangKeyOf(k);
    };
    CHECK_EQ(code(30, 'a'), std::string("KeyA"));
    CHECK_EQ(code(26, '@'), std::string("BracketLeft"));
    CHECK_EQ(code(43), std::string("Backslash"));  // JIS ]
    CHECK_EQ(code(89), std::string("IntlRo"));
    CHECK_EQ(code(124), std::string("IntlYen"));
    CHECK_EQ(code(41), std::string("Backquote"));  // 半角/全角
    CHECK_EQ(code(85), std::string("Backquote"));
    CHECK_EQ(code(13), std::string("Equal"));      // JIS ^
    CHECK_EQ(code(42), std::string());              // Shift alone
    CHECK_EQ(code(58), std::string());              // Caps Lock
    CHECK_EQ(code(100), std::string());             // Right Alt
    CHECK_EQ(code(125), std::string());             // Super
    CHECK_EQ(code(65), std::string("F7"));
    CHECK_EQ(code(87), std::string("F11"));
    CHECK_EQ(code(194), std::string("F24"));
    CHECK_EQ(code(96), std::string("Enter"));       // the keypad's
    CHECK_EQ(code(105), std::string("ArrowLeft"));
    CHECK_EQ(code(71), std::string("Numpad7"));
    CHECK_EQ(code(111), std::string("Delete"));
    CHECK_EQ(code(0, 'a'), std::string("Unidentified"));  // a synthetic key
    CHECK_EQ(code(240), std::string("Unidentified"));

    CHECK(lang(94) == LangKey::kMuhenkan);
    CHECK(lang(92) == LangKey::kHenkan);
    CHECK(lang(93) == LangKey::kKatakana);
    CHECK(lang(90) == LangKey::kKatakana);
    CHECK(lang(123) == LangKey::kEisu);
    CHECK(lang(122) == LangKey::kKana);
    CHECK(lang(58, sym::kCapsLock) == LangKey::kCapsLock);
    CHECK(lang(58, sym::kEisuToggle) == LangKey::kCapsLock);
    CHECK(lang(0, sym::kHenkan) == LangKey::kHenkan);      // synthetic: by keysym
    CHECK(lang(0, sym::kEisuToggle) == LangKey::kCapsLock);
    CHECK(lang(30, sym::kHenkan) == LangKey::kNone);       // a code wins over a remapped keysym
    CHECK(lang(41) == LangKey::kNone);                     // 半角/全角 is the engine's

    CHECK_EQ(JisCharacter("KeyA", false, true), std::string("A"));
    CHECK_EQ(JisCharacter("KeyA", true, true), std::string("A"));  // v1.1 §7.3: ⇧ or not
    CHECK_EQ(JisCharacter("Digit2", true, false), std::string("\""));
    CHECK_EQ(JisCharacter("IntlRo", true, false), std::string("_"));
    CHECK_EQ(JisCharacter("Space", false, false), std::string());
}

void TestManifest() {
    std::string error;
    auto m = ParseManifest(R"({"name": "x", "library": "{arch}/x.{ext}", "prefix": "x"})", "/base", &error);
    CHECK(m.has_value());
    if (m) {
        CHECK_EQ(m->id, std::string("bada-x"));
        CHECK_EQ(m->display_name, std::string("x"));
        CHECK_EQ(m->language, std::string("ja"));
        CHECK_EQ(m->layout, std::string("jp"));
        CHECK_EQ(m->mode, std::string("henkan"));
        const std::string lib = LibraryPath(*m).generic_string();
#if defined(__APPLE__)
        CHECK(lib.find("/x.dylib") != std::string::npos);
#else
        CHECK(lib.find("/x.so") != std::string::npos);
#endif
#if defined(__aarch64__)
        CHECK(lib.find("/aarch64/") != std::string::npos);
#endif
        CHECK(lib.find("{") == std::string::npos);
    }
    auto l = ParseManifest(R"({"name": "x", "library": "x", "prefix": "x", "linux": {"id": "my-ime",
        "display_name": "My IME", "language": "ko", "layout": "us", "labels": {"henkan": "가", "muhenkan": "A"},
        "icon": "my-icon"}})", "/", &error);
    CHECK(l && l->id == "my-ime" && l->display_name == "My IME" && l->language == "ko" && l->layout == "us" &&
          l->label_henkan == "가" && l->icon == "my-icon");
    CHECK(!ParseManifest(R"({"name": "x", "library": "x", "prefix": "x", "linux": {"id": "Bad Id"}})", "/", &error));
    CHECK(error.find("linux.id") != std::string::npos);
    CHECK(!ParseManifest(R"({"name": "X", "library": "x", "prefix": "x"})", "/", &error));  // bada-X
    CHECK(!ParseManifest(R"({"name": "x", "library": "x", "prefix": "x", "linux": []})", "/", &error));
    CHECK(!ParseManifest(R"({"name": "x", "library": "x", "prefix": "x", "mode": "kana"})", "/", &error));
    CHECK(!ParseManifest(R"({"name": "x", "library": "x"})", "/", &error));
    CHECK(IsImId("bada-shout") && IsImId("a.b_c-9") && !IsImId("") && !IsImId("9a") && !IsImId("a b"));

    // Data: files, null, concatenations with text and a folder (superkeyboard's rules).
    const fs::path dir = fs::temp_directory_path() / "bl-core-tests";
    fs::remove_all(dir);
    fs::create_directories(dir / "brands");
    std::ofstream(dir / "a.txt", std::ios::binary) << "alpha";
    std::ofstream(dir / "brands" / "2.tsv", std::ios::binary) << "two\n";
    std::ofstream(dir / "brands" / "1.tsv", std::ios::binary) << "one";
    std::ofstream(dir / "brands" / "skip.txt", std::ios::binary) << "no";
    auto d = ParseManifest(R"({"name": "x", "library": "x", "prefix": "x",
        "data": ["a.txt", null, {"file": "a.txt"}, {"concat": ["a.txt", {"text": "#! b\n"}, {"dir": "brands", "suffix": ".tsv"}]}]})",
                           dir, &error);
    std::vector<std::string> buffers;
    CHECK(d && LoadData(*d, &buffers, &error));
    CHECK_EQ(buffers.size(), size_t{4});
    if (buffers.size() == 4) CHECK_EQ(buffers[3], std::string("alpha\n#! b\none\ntwo\n"));
    fs::remove_all(dir);
}

void TestPublishedState() {
    HostInfo info;
    info.ime = "shout";
    info.version = "0.1.0";
    info.frontend = "fcitx5";
    info.app = "gedit";
    const std::string engine = R"({"format":"bada-compatible-state/1","mode":"henkan","profile":null,
        "staged":"HELLO","preedit":"HELLO","menu":["HELLO","hello"],"selected":0,"zero":0,
        "horizontal":false,"need_selection":null,"host":{"name":"forged"}})";
    auto open = json::Parse(PublishedState(engine, info));
    CHECK(open && open->GetString("staged") == "HELLO");
    CHECK(open && open->Get("host")->GetString("name") == "bada-linux");
    CHECK(open && open->Get("host")->GetString("frontend") == "fcitx5");
    info.secure = true;
    const std::string secure = PublishedState(engine, info);
    CHECK(secure.find("HELLO") == std::string::npos);
    auto s = json::Parse(secure);
    CHECK(s && s->Get("host")->GetBool("secure") && s->Get("menu")->array.empty());
}

void TestSettings() {
    Manifest m;
    m.id = "bada-x";
    Settings d = ParseSettings("{}", m);
    CHECK(d.host.caps == CapsSetting::kRespect && d.host.pass_on == PassOn::kJis);
    CHECK_EQ(d.host.kana_profile, std::string("mac-kana"));
    CHECK(d.host.ralt_profile.empty());  // Right Alt: off by default
    CHECK(d.publish_state);
    Settings s = ParseSettings(R"({"caps_lock": "toggle", "caps_rule": "uppersnake", "pass_on": "system",
        "kana_key": "none", "right_alt_tap": "mac-kana", "engine_settings": {"forms": ["lower"]},
        "publish_state": false, "off_in": ["keepassxc", 3]})", m);
    CHECK(s.host.caps == CapsSetting::kToggle && s.host.caps_rule == CapsRule::kUpperSnake);
    CHECK(s.host.pass_on == PassOn::kSystem && s.host.kana_profile.empty());
    CHECK_EQ(s.host.ralt_profile, std::string("mac-kana"));
    CHECK(!s.publish_state && s.off_in.size() == 1 && s.engine.IsObject());
    m.keyboard = "us";
    CHECK(ParseSettings("not json", m).host.pass_on == PassOn::kSystem);

    // The file: read once, again only when it changed.
    const fs::path dir = fs::temp_directory_path() / "bl-settings-tests";
    fs::remove_all(dir);
    setenv("XDG_CONFIG_HOME", dir.c_str(), 1);
    m.keyboard = "jis";
    SettingsFile file(m);
    CHECK_EQ(file.path(), dir / "bada-linux" / "bada-x.json");
    Settings out;
    CHECK(file.Reload(&out));
    CHECK(!file.Reload(&out));
    fs::create_directories(dir / "bada-linux");
    std::ofstream(file.path()) << R"({"caps_lock": "transient"})";
    CHECK(file.Reload(&out));
    CHECK(out.host.caps == CapsSetting::kTransient);
    CHECK(!file.Reload(&out));
    fs::remove(file.path());
    CHECK(file.Reload(&out));
    CHECK(out.host.caps == CapsSetting::kRespect);
    fs::remove_all(dir);
    unsetenv("XDG_CONFIG_HOME");
}

void TestPublisher() {
    const fs::path dir = fs::temp_directory_path() / "bl-publish-tests";
    fs::remove_all(dir);
    fs::create_directories(dir);
    setenv("XDG_RUNTIME_DIR", dir.c_str(), 1);
    const fs::path path = Publisher::StatePath("bada-x");
    CHECK_EQ(path, dir / "bada-linux" / "bada-x" / "state.json");
    {
        Publisher p("bada-x");
        p.Publish(R"({"mode":"henkan"})");
        std::string text;
        CHECK(ReadFile(path, &text) && text == "{\"mode\":\"henkan\"}\n");
        CHECK((fs::status(path).permissions() & (fs::perms::group_all | fs::perms::others_all)) == fs::perms::none);
        CHECK((fs::status(path.parent_path()).permissions() & fs::perms::others_all) == fs::perms::none);
        p.Clear();
        CHECK(!fs::exists(path));
    }
    fs::remove_all(dir);
    unsetenv("XDG_RUNTIME_DIR");
}

std::string g_shout;

Manifest ShoutManifest() {
    std::string error;
    auto m = ParseManifest("{\"name\": \"shout\", \"library\": " + json::Quote(g_shout) +
                               ", \"prefix\": \"shout\", \"settings\": {\"forms\": [\"upper\", \"lower\"]}}",
                           ".", &error);
    if (!m) {
        std::printf("FAIL shout manifest: %s\n", error.c_str());
        ++failures;
        return {};
    }
    return *m;
}

std::unique_ptr<Module> LoadShout() {
    std::string error;
    auto module = Module::Load(ShoutManifest(), &error);
    if (!module) {
        std::printf("FAIL loading %s: %s\n", g_shout.c_str(), error.c_str());
        ++failures;
    }
    return module;
}

void TestRightAlt(const Module& module) {
    Engine engine(module);
    HostSettings s;
    Host host(engine, s, Mode::kMuhenkan);
    TextField field;
    Typist t{host, field};
    host.SwitchMode(Mode::kMuhenkan, field);
    t.Tap("ralt");  // off by default: nothing
    CHECK(host.mode() == Mode::kMuhenkan);
    s.ralt_profile = kProfileMacKana;
    host.set_settings(s);
    t.Tap("ralt");  // a tap: henkan, profile mac-kana's key
    CHECK(host.mode() == Mode::kHenkan);
    t.Type("KeyH KeyI");
    t.Tap("ralt");  // in henkan, with no profiles: the next entry
    CHECK_EQ(field.preedit(), std::string("hi"));
    // Right Alt held for a shortcut isn't a tap.
    t.Press("ralt");
    t.Press("A-KeyA");
    host.KeyUp(Parse("ralt"), field);
    CHECK(host.mode() == Mode::kHenkan);
    // MEH + a Right Alt tap isn't one either: ⇧ ⌃ ⌘ cancel it.
    t.Press("muhenkan");
    t.Press("C-ralt");
    host.KeyUp(Parse("ralt"), field);
    CHECK(host.mode() == Mode::kMuhenkan);
}

// A sink that keeps what's published.
struct Sink final : StateSink {
    std::vector<std::string> published;
    int cleared = 0;
    void Publish(const std::string& json) override { published.push_back(json); }
    void Clear() override { ++cleared; }
};

void TestSession() {
    const fs::path dir = fs::temp_directory_path() / "bl-session-tests";
    fs::remove_all(dir);
    setenv("XDG_CONFIG_HOME", (dir / "config").c_str(), 1);
    setenv("XDG_DATA_HOME", (dir / "data").c_str(), 1);
    Sink sink;
    std::string error;
    auto session = Session::Load(ShoutManifest(), "test", "9.9.9", &sink, &error);
    CHECK(session != nullptr);
    if (!session) return;
    CHECK(session->mode() == Mode::kHenkan);
    CHECK_EQ(session->Label(), std::string("\xE3\x81\x82"));

    Context a(*session), b(*session);
    TextField fa, fb;
    const ContextInfo info{"gedit", false};
    a.FocusIn(fa, info);
    CHECK(!sink.published.empty());
    auto key = [&](Context& c, TextField& f, const std::string& token, const ContextInfo& i) {
        KeyDown k = Parse(token);
        const bool eaten = c.Key(k, f, i);
        if (!eaten) f.App(CodeOf(k), k);
        return eaten;
    };
    key(a, fa, "KeyH", info);
    key(a, fa, "KeyI", info);
    CHECK_EQ(fa.preedit(), std::string("HI"));
    auto state = json::Parse(sink.published.back());
    CHECK(state && state->GetString("staged") == "HI" && state->Get("host")->GetString("app") == "gedit");
    CHECK(state && state->Get("host")->GetString("version") == "9.9.9");
    // 無変換 in a: commits there; the session is muhenkan, b follows when it next sees a key.
    key(a, fa, "muhenkan", info);
    CHECK_EQ(fa.text(), std::string("HI"));
    CHECK(session->mode() == Mode::kMuhenkan);
    CHECK_EQ(session->Label(), std::string("A"));
    b.FocusIn(fb, info);
    key(b, fb, "KeyX", info);
    CHECK_EQ(fb.text(), std::string("x"));
    CHECK(b.host().mode() == Mode::kMuhenkan);
    // b back to henkan; a follows, committing nothing (nothing staged).
    key(b, fb, "henkan", info);
    key(a, fa, "KeyY", info);
    CHECK_EQ(fa.preedit(), std::string("Y"));
    // Focus out commits.
    a.FocusOut(fa);
    CHECK_EQ(fa.text(), std::string("HIY"));

    // A password field: nothing composes, nothing typed is published; language keys still switch.
    const ContextInfo secure{"gedit", true};
    key(a, fa, "KeyP", secure);
    CHECK_EQ(fa.text(), std::string("HIYp"));
    CHECK(sink.published.back().find("\"secure\":true") != std::string::npos);
    key(a, fa, "muhenkan", secure);
    CHECK(session->mode() == Mode::kMuhenkan);
    key(a, fa, "henkan", info);

    // Settings from the file: engine settings merge over the manifest's; off in a program.
    fs::create_directories(dir / "config" / "bada-linux");
    std::ofstream(dir / "config" / "bada-linux" / "bada-shout.json")
        << R"({"engine_settings": {"forms": ["upper", "lower", "title"]}, "off_in": ["keepassxc"]})";
    Context c(*session);
    TextField fc;
    c.FocusIn(fc, info);
    key(c, fc, "KeyH", info);
    key(c, fc, "KeyI", info);
    CHECK_EQ(fc.menu.size(), size_t{3});
    c.FocusOut(fc);
    const ContextInfo off{"keepassxc", false};
    key(c, fc, "KeyQ", off);
    CHECK_EQ(fc.text(), std::string("HIq"));
    CHECK(sink.published.back().find("\"off\":true") != std::string::npos);

    // Publishing off: the sink is cleared, and nothing more is published.
    std::ofstream(dir / "config" / "bada-linux" / "bada-shout.json") << R"({"publish_state": false})";
    const size_t before = sink.published.size();
    c.FocusIn(fc, info);
    CHECK_EQ(sink.cleared, 1);
    key(c, fc, "KeyZ", info);
    CHECK_EQ(sink.published.size(), before);
    fs::remove_all(dir);
    unsetenv("XDG_CONFIG_HOME");
    unsetenv("XDG_DATA_HOME");
}

void TestShout(const Module& module) {
    CHECK_EQ(KeyEventJson("KeyA", Mods{true, false, false, false}),
             std::string(R"({"key":"KeyA","shift":true,"ctrl":false,"alt":false,"meta":false})"));
    {
        // superkeyboard's shout test: HELLO world, then muhenkan by 無変換.
        Engine engine(module);
        CHECK(engine.ok() && !engine.HasProfiles() && engine.HasState());
        Host host(engine, HostSettings{}, Mode::kHenkan);
        TextField field;
        Typist t{host, field};
        host.SwitchMode(Mode::kHenkan, field);
        t.Type("KeyH KeyE KeyL KeyL KeyO");
        CHECK_EQ(field.preedit(), std::string("HELLO"));
        CHECK_EQ(field.menu.size(), size_t{2});
        auto state = json::Parse(host.EngineState());
        CHECK(state && state->GetString("staged") == "HELLO" && state->GetString("mode") == "henkan");
        t.Type("Space KeyW KeyO KeyR KeyL KeyD Tab Enter");
        CHECK_EQ(field.text(), std::string("HELLO world\n"));
        t.Type("muhenkan KeyA KeyB KeyC Backspace");
        CHECK(host.mode() == Mode::kMuhenkan);
        CHECK(host.TakeStateChanged());
        CHECK_EQ(field.text(), std::string("HELLO world\nab"));
        CHECK_EQ(field.typed, 3);  // the host typed the JIS characters, the app the Backspace
        state = json::Parse(host.EngineState());
        CHECK(state && state->GetString("staged", "x").empty() && state->GetString("mode") == "muhenkan");
    }
    {
        // 変換 is the home key: from muhenkan it enters henkan; with a menu it moves in it (⇧: back).
        Engine engine(module);
        Host host(engine, HostSettings{}, Mode::kMuhenkan);
        TextField field;
        Typist t{host, field};
        host.SwitchMode(Mode::kMuhenkan, field);
        t.Type("henkan KeyH KeyI");
        CHECK(host.mode() == Mode::kHenkan);
        CHECK_EQ(field.preedit(), std::string("HI"));
        t.Type("henkan");
        CHECK_EQ(field.preedit(), std::string("hi"));
        t.Type("S-henkan");
        CHECK_EQ(field.preedit(), std::string("HI"));
        t.Type("katakana");  // no profiles in shout: next entry too
        CHECK_EQ(field.preedit(), std::string("hi"));
        t.Type("eisu");  // 英数 commits, then muhenkan
        CHECK(host.mode() == Mode::kMuhenkan);
        CHECK_EQ(field.text(), std::string("hi"));
        t.Type("kana KeyX Period");  // かな: henkan (mac-kana); Period commits, then types
        CHECK_EQ(field.text(), std::string("hiX."));
        t.Type("C-muhenkan C-A-S-henkan");  // MEH + 変換: the other mode
        CHECK(host.mode() == Mode::kHenkan);
    }
    {
        // 半角/全角 in muhenkan shouts the selection (need_selection → P_host_selection).
        Engine engine(module);
        Host host(engine, HostSettings{}, Mode::kMuhenkan);
        TextField field;
        Typist t{host, field};
        host.SwitchMode(Mode::kMuhenkan, field);
        t.Type("KeyQ KeyU KeyI KeyE KeyT Space KeyP KeyL KeyE KeyA KeyS KeyE");
        field.Select(6, 12);
        t.Type("zenkaku");
        CHECK_EQ(field.text(), std::string("quiet PLEASE"));
    }
    {
        // Caps Lock: respect (default) leaves it to the system; letters are capitals, ⇧ or not.
        Engine engine(module);
        Host host(engine, HostSettings{}, Mode::kMuhenkan);
        TextField field;
        Typist t{host, field};
        host.SwitchMode(Mode::kMuhenkan, field);
        t.Type("caps KeyA S-KeyB caps KeyC");
        CHECK_EQ(field.text(), std::string("ABc"));
        CHECK(host.mode() == Mode::kMuhenkan);
    }
    {
        // toggle: Caps Lock switches mode and stays off; ⇧ + Caps Lock is Caps Lock in muhenkan.
        Engine engine(module);
        HostSettings s;
        s.caps = CapsSetting::kToggle;
        Host host(engine, s, Mode::kMuhenkan);
        TextField field;
        Typist t{host, field};
        host.SwitchMode(Mode::kMuhenkan, field);
        t.Type("caps");
        CHECK(host.mode() == Mode::kHenkan);
        CHECK(!t.caps);
        t.Type("KeyH KeyI caps");
        CHECK(host.mode() == Mode::kMuhenkan);
        CHECK_EQ(field.text(), std::string("HI"));
        t.Type("S-caps KeyX");
        CHECK(t.caps);
        CHECK_EQ(field.text(), std::string("HIX"));
        t.Type("eisu-caps");  // the same key under the JIS layout (英数): toggles too
        CHECK(host.mode() == Mode::kHenkan);
        CHECK(!t.caps);  // leaving muhenkan forgets Caps Lock
        t.Type("muhenkan S-caps henkan");
        CHECK(!t.caps);
        CHECK(host.mode() == Mode::kHenkan);
    }
    {
        // respect, under the jp layout: the unshifted Caps Lock key is 英数 (Eisu_toggle), muhenkan.
        Engine engine(module);
        Host host(engine, HostSettings{}, Mode::kHenkan);
        TextField field;
        Typist t{host, field};
        host.SwitchMode(Mode::kHenkan, field);
        t.Type("KeyA eisu-caps");
        CHECK(host.mode() == Mode::kMuhenkan);
        CHECK_EQ(field.text(), std::string("A"));
        t.Type("eisu-caps");
        CHECK(host.mode() == Mode::kMuhenkan);
    }
    {
        // transient: Caps Lock in muhenkan only; in henkan it does nothing and stays off.
        Engine engine(module);
        HostSettings s;
        s.caps = CapsSetting::kTransient;
        Host host(engine, s, Mode::kHenkan);
        TextField field;
        Typist t{host, field};
        host.SwitchMode(Mode::kHenkan, field);
        t.Type("caps");
        CHECK(!t.caps);
        CHECK(host.mode() == Mode::kHenkan);
        t.Type("muhenkan caps KeyA henkan");
        CHECK_EQ(field.text(), std::string("A"));
        CHECK(!t.caps);
        t.caps = true;  // another program turned it on in henkan
        t.Type("KeyB");
        CHECK(!t.caps);
    }
    {
        // UPPER_SNAKE: Space types _, ⌥Space a space, in muhenkan with Caps Lock on.
        Engine engine(module);
        HostSettings s;
        s.caps_rule = CapsRule::kUpperSnake;
        Host host(engine, s, Mode::kMuhenkan);
        TextField field;
        Typist t{host, field};
        host.SwitchMode(Mode::kMuhenkan, field);
        t.Type("caps KeyA Space KeyB A-Space KeyC caps Space");
        CHECK_EQ(field.text(), std::string("A_B C "));
    }
    {
        // The system's layout types what the engine passes on (pass_on = system).
        Engine engine(module);
        HostSettings s;
        s.pass_on = PassOn::kSystem;
        Host host(engine, s, Mode::kMuhenkan);
        TextField field;
        Typist t{host, field};
        host.SwitchMode(Mode::kMuhenkan, field);
        t.Type("KeyA S-Digit2");
        CHECK_EQ(field.text(), std::string("a\""));
        CHECK_EQ(field.typed, 0);
        CHECK_EQ(field.app_typed, 2);
        // A password field: nothing reaches the engine.
        HostSettings jis;
        host.set_settings(jis);
        KeyDown k;
        k.code = 37;
        CHECK(host.TypeOnly(k, field));
        CHECK_EQ(field.text(), std::string("a\"k"));
    }
    {
        // A modifier on its own never commits; a shortcut commits, then goes to the app.
        Engine engine(module);
        Host host(engine, HostSettings{}, Mode::kHenkan);
        TextField field;
        Typist t{host, field};
        host.SwitchMode(Mode::kHenkan, field);
        t.Type("KeyO KeyK Shift");
        CHECK_EQ(field.preedit(), std::string("OK"));
        CHECK(!t.Press("C-KeyA"));
        CHECK_EQ(field.text(), std::string("OK"));
        CHECK(!host.composing());
    }
    {
        // Engines on one Data, on two threads' worth of hosts: independent.
        Engine a(module), b(module);
        Host ha(a, HostSettings{}, Mode::kHenkan), hb(b, HostSettings{}, Mode::kHenkan);
        TextField fa, fb;
        Typist ta{ha, fa}, tb{hb, fb};
        ha.SwitchMode(Mode::kHenkan, fa);
        hb.SwitchMode(Mode::kMuhenkan, fb);
        ta.Type("KeyX");
        tb.Type("KeyX");
        CHECK_EQ(fa.preedit(), std::string("X"));
        CHECK_EQ(fb.text(), std::string("x"));
    }
}

}  // namespace

int main(int argc, char** argv) {
    TestJson();
    TestOutput();
    TestKeys();
    TestManifest();
    TestPublishedState();
    TestSettings();
    TestPublisher();
    g_shout = argc > 1 ? argv[1] : "";
    if (g_shout.empty()) {
        if (const char* env = std::getenv("SHOUT_LIB")) g_shout = env;
    }
    if (g_shout.empty()) {
        std::printf("skipped the engine tests: no shout library (argument or SHOUT_LIB)\n");
    } else if (auto module = LoadShout()) {
        TestShout(*module);
        TestRightAlt(*module);
        TestSession();
    }
    std::printf("%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
