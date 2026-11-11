// The host side of bada-compatible v1.1 (bada-compatible docs/bada-compatible-v1.md §7, §12),
// for one Linux input method that holds both modes (docs/spec.md): mode and profile switching by the
// JIS language keys, Caps Lock (the host's, §7.7 there), the characters typed for keys the engine
// passes on, and the state published for other programs. Framework-free: the Fcitx5 and IBus
// frontends drive it with their input contexts as the Field, the core tests with a plain text model.
#pragma once

#include <optional>
#include <utility>
#include <string>

#include "engine.h"
#include "keys.h"
#include "output.h"

namespace bl {

// Henkan profile names (bada spec §6.6). On a PC keyboard 変換 is the home key, so "henkan" is the
// default profile (docs/spec.md §4.2).
inline constexpr const char* kProfileHenkan = "henkan";
inline constexpr const char* kProfileKatakana = "katakana";
inline constexpr const char* kProfileMacKana = "mac-kana";

// What Caps Lock does (bada spec §6.7; optional here, off by default: docs/spec.md §5).
enum class CapsSetting {
    kRespect,    // the system's Caps Lock, untouched (the default)
    kToggle,     // Caps Lock switches mode; ⇧ ⌃ ⌥ + Caps Lock is Caps Lock, in muhenkan only
    kTransient,  // Caps Lock in muhenkan only, off in henkan and when muhenkan is left
};
// What Caps Lock types in muhenkan (bada spec §6.2): ALL CAPS, or UPPER_SNAKE (Space types _).
enum class CapsRule { kAllCaps, kUpperSnake };
// Who types a character key the engine passes on: the host, from its JIS table (the default, as
// bada's hosts do), or the app, through the system's keyboard layout.
enum class PassOn { kJis, kSystem };

struct HostSettings {
    CapsSetting caps = CapsSetting::kRespect;
    CapsRule caps_rule = CapsRule::kAllCaps;
    PassOn pass_on = PassOn::kJis;
    // The profile an Apple keyboard's かな key brings up; "": the key goes to the app.
    std::string kana_profile = kProfileMacKana;
    // The profile a Right Alt tap (pressed and released alone) brings up; "": none (the default).
    std::string ralt_profile;
};

// The text field, as the host needs it. Outputs are applied in the order given.
class Field {
public:
    virtual ~Field() = default;
    // bada-compatible §7.1, without the need_selection step (the host does that).
    virtual void Apply(const Output& out) = 0;
    // The selected text ("" when nothing is selected).
    virtual std::string Selection() = 0;
    // Inserts `text` at the selection, as typed (a character the engine passed on).
    virtual void Type(const std::string& text) = 0;
};

class Host {
public:
    Host(Engine& engine, HostSettings settings, Mode mode);

    // A key press. True: eaten (the app doesn't get it).
    bool Key(const KeyDown& key, Field& field);
    // A key release. Never eaten (the app saw the press); a Right Alt tap acts here.
    void KeyUp(const KeyDown& key, Field& field);
    // A key-down where nothing may compose (a password field, a program the IME is off in): only
    // the JIS character, if the host types them. True: eaten.
    bool TypeOnly(const KeyDown& key, Field& field);

    // The engine's first P_set_mode (and P_set_profile), for the mode and profile given.
    void Start(Mode mode, const std::string& profile, Field& field);

    // The host's own switches (a key, the indicator, the menu): mark the session state changed.
    void SwitchMode(Mode mode, Field& field);
    void SwitchProfile(const std::string& name, Field& field);
    // The session's state changed in another thread or app: follow it, committing what's staged.
    void Follow(Mode mode, const std::string& profile, Field& field);
    // Commits what's staged (focus lost, IME switched).
    void Flush(Field& field);
    // Forgets the menu and the staged flag without telling the field (a composition the app ended,
    // after the engine was flushed into nothing).
    void Forget();

    Mode mode() const { return mode_; }
    const std::string& profile() const { return profile_; }
    bool composing() const { return composing_; }
    const Output& last() const { return last_; }
    const HostSettings& settings() const { return settings_; }
    void set_settings(const HostSettings& settings) { settings_ = settings; }

    // After a call: whether this host changed the mode or profile (the shell publishes it to the
    // session), and the Caps Lock state wanted (the shell sets it if it differs). Each is taken once.
    bool TakeStateChanged();
    std::optional<bool> TakeCapsWanted();

    // The engine's state (P_state, §12), as of the last call that returned an Output; synthesized
    // from the last Output when the engine has no P_state ("staged" is then null: unknown).
    std::string EngineState();

private:
    void Apply(const Output& out, Field& field);
    void ProfileKey(const std::string& profile, const KeyDown& key, Field& field);
    bool CapsKey(const KeyDown& key, Field& field);
    bool PassOnKey(const std::string& code, const KeyDown& key, Field& field);

    Engine& engine_;
    HostSettings settings_;
    Mode mode_;
    std::string profile_ = kProfileHenkan;
    bool composing_ = false;  // the last Output left a preedit
    bool ralt_tap_ = false;   // Right Alt is down, and nothing else was pressed since
    Output last_;             // the last Output applied
    bool state_changed_ = false;
    std::optional<bool> caps_wanted_;
};

// What other programs read (docs/api.md): the engine's state with the host's own fields under
// "host" (bada-compatible §12.1). `secure`: the focused field is a password field (secure input):
// "staged", "preedit" and "menu" are emptied and host.secure is true.
struct HostInfo {
    std::string ime;      // the manifest's name
    std::string version;  // bada-linux'
    std::string frontend; // "fcitx5", "ibus"
    std::string app;      // the program in focus, as the framework names it ("firefox"); "" unknown
    Mode mode = Mode::kHenkan;
    std::string profile;
    bool profiles = false;  // the engine has P_set_profile
    bool caps_lock = false;
    bool secure = false;
    bool off = false;  // the IME is off in this program
};
std::string PublishedState(const std::string& engine_state, const HostInfo& host);

}  // namespace bl
