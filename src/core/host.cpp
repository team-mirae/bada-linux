#include "host.h"

#include "json.h"

namespace bl {
namespace {

Mode Other(Mode m) { return m == Mode::kHenkan ? Mode::kMuhenkan : Mode::kHenkan; }

json::Value Str(const std::string& s) {
    json::Value v;
    v.type = json::Value::Type::kString;
    v.string = s;
    return v;
}

json::Value Bool(bool b) {
    json::Value v;
    v.type = json::Value::Type::kBool;
    v.boolean = b;
    return v;
}

json::Value Num(double d) {
    json::Value v;
    v.type = json::Value::Type::kNumber;
    v.number = d;
    return v;
}

json::Value Array(const std::vector<std::string>& rows) {
    json::Value v;
    v.type = json::Value::Type::kArray;
    for (const std::string& row : rows) v.array.push_back(Str(row));
    return v;
}

const char* ModeName(Mode m) { return m == Mode::kHenkan ? "henkan" : "muhenkan"; }

}  // namespace

Host::Host(Engine& engine, HostSettings settings, Mode mode)
    : engine_(engine), settings_(std::move(settings)), mode_(mode) {}

bool Host::TakeStateChanged() {
    const bool changed = state_changed_;
    state_changed_ = false;
    return changed;
}

std::optional<bool> Host::TakeCapsWanted() {
    std::optional<bool> wanted;
    wanted.swap(caps_wanted_);
    return wanted;
}

void Host::Apply(const Output& out, Field& field) {
    field.Apply(out);
    last_ = out;
    composing_ = !out.preedit.empty();
    // bada-compatible §7.5: the selected text (none while something is marked), answered once.
    if (out.need_selection && engine_.HasHostSelection()) {
        const std::string text = composing_ ? std::string() : field.Selection();
        const Output reply = engine_.HostSelection(text);
        field.Apply(reply);
        last_ = reply;
        composing_ = !reply.preedit.empty();
    }
}

void Host::Forget() {
    composing_ = false;
    last_ = {};
}

bool Host::Key(const KeyDown& key, Field& field) {
    ralt_tap_ = key.code == ev::kRightAlt && !key.repeat && !key.mods.shift && !key.mods.ctrl &&
                !key.mods.meta;
    switch (LangKeyOf(key)) {
        case LangKey::kMuhenkan:
        case LangKey::kEisu:
            if (!key.repeat) SwitchMode(Mode::kMuhenkan, field);
            return true;
        case LangKey::kHenkan:
            ProfileKey(kProfileHenkan, key, field);
            return true;
        case LangKey::kKatakana:
            ProfileKey(kProfileKatakana, key, field);
            return true;
        case LangKey::kKana:
            if (settings_.kana_profile.empty()) return false;
            ProfileKey(settings_.kana_profile, key, field);
            return true;
        case LangKey::kCapsLock:
            return CapsKey(key, field);
        case LangKey::kNone:
            break;
    }
    const std::string code = CodeOf(key);
    if (code.empty()) return false;  // a modifier on its own: the next key's
    KeyDown k = key;
    // Caps Lock is off in henkan unless it's the system's (respect): turned back off if another program
    // turned it on (bada spec §6.7), and never applied meanwhile.
    if (mode_ == Mode::kHenkan && settings_.caps != CapsSetting::kRespect && k.caps) {
        caps_wanted_ = false;
        k.caps = false;
    }
    // UPPER_SNAKE (bada-compatible §7.7): Space types _, ⌥Space a space, without P_key.
    if (mode_ == Mode::kMuhenkan && k.caps && settings_.caps_rule == CapsRule::kUpperSnake &&
        code == "Space" && !k.mods.ctrl && !k.mods.meta) {
        Apply(engine_.Flush(), field);
        field.Type(k.mods.alt ? " " : "_");
        return true;
    }
    const Output out = engine_.Key(code, k.mods);
    Apply(out, field);
    if (out.consumed) return true;
    return PassOnKey(code, k, field);
}

bool Host::TypeOnly(const KeyDown& key, Field& field) {
    if (LangKeyOf(key) != LangKey::kNone) return false;
    const std::string code = CodeOf(key);
    return !code.empty() && PassOnKey(code, key, field);
}

bool Host::PassOnKey(const std::string& code, const KeyDown& key, Field& field) {
    // bada-compatible §7.3: a character key types its JIS legend. Shortcuts (Ctrl, Alt, Win) and keys
    // without a character (Space, Enter, arrows, F keys) are the app's.
    if (settings_.pass_on != PassOn::kJis || !key.mods.Plain()) return false;
    const std::string text = JisCharacter(code, key.mods.shift, key.caps);
    if (text.empty()) return false;
    field.Type(text);
    return true;
}

void Host::ProfileKey(const std::string& profile, const KeyDown& key, Field& field) {
    if (key.repeat) return;  // held down: one press
    const Mods& m = key.mods;
    // ⌃⌥⇧ (MEH) + a profile key: the other mode (bada-compatible §7.4's ⌃⌥⇧ Lang1).
    if (m.ctrl && m.alt && m.shift) {
        SwitchMode(Other(mode_), field);
        return;
    }
    if (m.ctrl || m.alt || m.meta) return;
    if (mode_ == Mode::kMuhenkan) {
        if (engine_.HasProfiles()) profile_ = profile;
        SwitchMode(Mode::kHenkan, field);
        return;
    }
    // In henkan: the same profile's key moves in the menu (⇧: back); so does the home key 変換 in
    // another profile while a menu of more than one row shows; another profile's key switches to it
    // (bada spec §6.6, with 変換 in かな's place: docs/spec.md §4.2).
    if (!engine_.HasProfiles() || profile == profile_ ||
        (profile == kProfileHenkan && last_.menu.size() > 1)) {
        Apply(engine_.Key("Lang1", Mods{m.shift, false, false, false}), field);
        return;
    }
    SwitchProfile(profile, field);
}

bool Host::CapsKey(const KeyDown& key, Field& field) {
    const Mods& m = key.mods;
    const bool modified = m.shift || m.ctrl || m.alt || m.meta;
    if (settings_.caps == CapsSetting::kToggle && !modified) {
        if (!key.repeat) SwitchMode(Other(mode_), field);
        caps_wanted_ = false;  // a mode switch by Caps Lock leaves Caps Lock off
        return true;
    }
    // The XKB jp layout's unshifted Caps Lock key is 英数 (Eisu_toggle): no lock changes.
    if (key.sym == sym::kEisuToggle) {
        if (!key.repeat) SwitchMode(Mode::kMuhenkan, field);
        return true;
    }
    if (settings_.caps == CapsSetting::kRespect || mode_ == Mode::kMuhenkan) return false;  // the system's
    caps_wanted_ = false;  // henkan: Caps Lock does nothing, and stays off
    return true;
}

void Host::KeyUp(const KeyDown& key, Field& field) {
    const bool tap = ralt_tap_ && key.code == ev::kRightAlt;
    ralt_tap_ = false;
    if (!tap || settings_.ralt_profile.empty()) return;
    KeyDown press;
    press.code = ev::kRightAlt;
    ProfileKey(settings_.ralt_profile, press, field);
}

void Host::Start(Mode mode, const std::string& profile, Field& field) {
    if (!profile.empty()) profile_ = profile;
    mode_ = mode;
    Apply(engine_.SetMode(mode), field);
    if (engine_.HasProfiles()) Apply(engine_.SetProfile(profile_), field);
}

void Host::SwitchMode(Mode mode, Field& field) {
    Apply(engine_.SetMode(mode), field);
    mode_ = mode;
    // bada-compatible §7.4: after every P_set_mode, P_set_profile with the current profile.
    if (engine_.HasProfiles()) Apply(engine_.SetProfile(profile_), field);
    state_changed_ = true;
    if (mode == Mode::kHenkan && settings_.caps != CapsSetting::kRespect) caps_wanted_ = false;
}

void Host::SwitchProfile(const std::string& name, Field& field) {
    if (!engine_.HasProfiles()) return;
    profile_ = name;
    Apply(engine_.SetProfile(name), field);
    state_changed_ = true;
}

void Host::Follow(Mode mode, const std::string& profile, Field& field) {
    const bool profile_changed = engine_.HasProfiles() && !profile.empty() && profile != profile_;
    if (mode == mode_ && !profile_changed) return;
    if (!profile.empty()) profile_ = profile;
    if (mode != mode_) {
        Apply(engine_.SetMode(mode), field);
        mode_ = mode;
        if (engine_.HasProfiles()) Apply(engine_.SetProfile(profile_), field);
    } else {
        Apply(engine_.SetProfile(profile_), field);
    }
}

void Host::Flush(Field& field) { Apply(engine_.Flush(), field); }

std::string Host::EngineState() {
    if (engine_.HasState()) {
        std::string state = engine_.State();
        if (!state.empty()) return state;
    }
    json::Value v;
    v.type = json::Value::Type::kObject;
    v.object["format"] = Str("bada-compatible-state/1");
    v.object["mode"] = Str(ModeName(mode_));
    if (engine_.HasProfiles()) v.object["profile"] = Str(profile_);
    else v.object["profile"] = json::Value{};
    v.object["staged"] = json::Value{};  // unknown: the engine has no P_state
    v.object["preedit"] = Str(last_.preedit);
    v.object["menu"] = Array(last_.menu);
    v.object["selected"] = Num(static_cast<double>(last_.selected));
    v.object["zero"] = Num(static_cast<double>(last_.zero));
    v.object["horizontal"] = Bool(last_.horizontal);
    v.object["need_selection"] = last_.need_selection ? Str(*last_.need_selection) : json::Value{};
    return json::Write(v);
}

std::string PublishedState(const std::string& engine_state, const HostInfo& host) {
    auto parsed = json::Parse(engine_state);
    json::Value v;
    if (parsed && parsed->IsObject()) {
        v = std::move(*parsed);
    } else {
        v.type = json::Value::Type::kObject;
        v.object["format"] = Str("bada-compatible-state/1");
    }
    v.object.erase("host");  // the host's fields are the host's alone
    if (host.secure) {
        // A password field (bada-compatible §12.1): nothing typed is published.
        v.object["staged"] = Str("");
        v.object["preedit"] = Str("");
        v.object["menu"] = Array({});
        v.object["notes"] = Array({});
        v.object["selected"] = Num(0);
        v.object["zero"] = Num(0);
        v.object["need_selection"] = json::Value{};
    }
    json::Value h;
    h.type = json::Value::Type::kObject;
    h.object["name"] = Str("bada-linux");
    h.object["frontend"] = Str(host.frontend);
    h.object["version"] = Str(host.version);
    h.object["ime"] = Str(host.ime);
    h.object["app"] = Str(host.app);
    h.object["mode"] = Str(ModeName(host.mode));
    h.object["profile"] = host.profiles ? Str(host.profile) : json::Value{};
    h.object["caps_lock"] = Bool(host.caps_lock);
    h.object["secure"] = Bool(host.secure);
    h.object["off"] = Bool(host.off);
    v.object["host"] = std::move(h);
    return json::Write(v);
}

}  // namespace bl
