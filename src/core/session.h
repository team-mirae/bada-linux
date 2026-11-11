// What both frontends share (docs/spec.md §3): one IME per process with one mode and profile for
// the whole session, and one engine per input context that follows them. A frontend makes one
// Session per manifest and one Context per input context, gives each call its Field (the input
// context) and what it knows about the field (ContextInfo), and does nothing else with the engine.
#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "engine.h"
#include "host.h"
#include "manifest.h"
#include "settings.h"

namespace bl {

// Where the published state goes (docs/api.md); the frontends give a file + D-Bus one.
class StateSink {
public:
    virtual ~StateSink() = default;
    virtual void Publish(const std::string& json) = 0;
    virtual void Clear() = 0;  // nothing is published (turned off)
};

struct ContextInfo {
    std::string app;      // the program, as the framework names it; "" unknown
    bool secure = false;  // a password or sensitive field: nothing composes or is published
};

class Session {
public:
    // Loads the engine library and its data, and the user dictionary if there is one. nullptr and
    // `error` on failure.
    static std::unique_ptr<Session> Load(const Manifest& manifest, std::string frontend,
                                         std::string version, StateSink* sink, std::string* error);

    const Manifest& manifest() const { return manifest_; }
    const Module& module() const { return *module_; }
    Mode mode() const { return mode_; }
    const std::string& profile() const { return profile_; }
    std::uint64_t generation() const { return generation_; }
    const Settings& settings() const { return settings_; }
    // The indicator's text for the session's mode (the manifest's labels).
    const std::string& Label() const;
    // Re-reads the settings file if it changed. True: changed.
    bool ReloadSettings();
    // For tests and frontends that hold settings elsewhere.
    void SetSettings(const Settings& settings);

private:
    friend class Context;
    Session(const Manifest& manifest) : manifest_(manifest), file_(manifest_) {}
    void Changed(Mode mode, const std::string& profile);

    Manifest manifest_;
    std::unique_ptr<Module> module_;
    SettingsFile file_;
    Settings settings_;
    std::uint64_t settings_generation_ = 0;
    Mode mode_ = Mode::kHenkan;
    std::string profile_ = kProfileHenkan;
    std::uint64_t generation_ = 0;  // bumped at every mode or profile change
    std::string frontend_, version_;
    StateSink* sink_ = nullptr;
};

class Context {
public:
    explicit Context(Session& session);

    bool ok() const { return engine_.ok(); }
    // A key press: true when eaten.
    bool Key(const KeyDown& key, Field& field, const ContextInfo& info);
    void KeyUp(const KeyDown& key, Field& field, const ContextInfo& info);
    // Focus came here: settings and the session's mode are taken up, the state published.
    void FocusIn(Field& field, const ContextInfo& info);
    // Focus left, or the app reset the field: what's staged is committed.
    void FocusOut(Field& field);
    // The indicator or a menu switched mode or profile.
    void SwitchMode(Mode mode, Field& field, const ContextInfo& info);
    void SwitchProfile(const std::string& profile, Field& field, const ContextInfo& info);
    // The menu and preedit the field shows were taken away without the engine (the app ended the
    // composition): forget them.
    void Forget() { host_.Forget(); }

    const Host& host() const { return host_; }
    // The published JSON, as it would be now.
    std::string State(const ContextInfo& info);
    // The engine's own state (P_state, or a stand-in), as of the last call.
    std::string EngineState() { return host_.EngineState(); }

private:
    void Sync(Field& field);       // follow the session's mode and settings
    void After(const ContextInfo& info);  // tell the session what changed, publish
    bool Off(const ContextInfo& info) const;

    Session& session_;
    Engine engine_;
    Host host_;
    std::uint64_t generation_ = 0;
    std::uint64_t settings_generation_ = 0;
    bool started_ = false;
};

}  // namespace bl
