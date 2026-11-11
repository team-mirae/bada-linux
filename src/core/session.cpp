#include "session.h"

#include <algorithm>

namespace bl {

std::unique_ptr<Session> Session::Load(const Manifest& manifest, std::string frontend, std::string version,
                                       StateSink* sink, std::string* error) {
    std::unique_ptr<Session> s(new Session(manifest));
    s->module_ = Module::Load(s->manifest_, error);
    if (!s->module_) return nullptr;
    s->frontend_ = std::move(frontend);
    s->version_ = std::move(version);
    s->sink_ = sink;
    s->mode_ = manifest.mode == "muhenkan" ? Mode::kMuhenkan : Mode::kHenkan;
    s->ReloadSettings();
    // The user dictionary, once per process (docs/spec.md §9).
    const std::filesystem::path dictionary =
        s->settings_.dictionary.empty() ? DictionaryPath(s->manifest_) : PathFromUtf8(s->settings_.dictionary);
    if (std::string text; ReadFile(dictionary, &text)) s->module_->LoadDictionary(text);
    return s;
}

const std::string& Session::Label() const {
    return mode_ == Mode::kHenkan ? manifest_.label_henkan : manifest_.label_muhenkan;
}

bool Session::ReloadSettings() {
    Settings s;
    if (!file_.Reload(&s)) return false;
    SetSettings(s);
    return true;
}

void Session::SetSettings(const Settings& settings) {
    settings_ = settings;
    ++settings_generation_;
    if (!settings_.publish_state && sink_) sink_->Clear();
}

void Session::Changed(Mode mode, const std::string& profile) {
    if (mode == mode_ && profile == profile_) return;
    mode_ = mode;
    profile_ = profile;
    ++generation_;
}

Context::Context(Session& session)
    : session_(session), engine_(session.module()), host_(engine_, session.settings().host, session.mode()) {}

bool Context::Off(const ContextInfo& info) const {
    const auto& off = session_.settings_.off_in;
    return !info.app.empty() && std::find(off.begin(), off.end(), info.app) != off.end();
}

void Context::Sync(Field& field) {
    if (settings_generation_ != session_.settings_generation_) {
        settings_generation_ = session_.settings_generation_;
        host_.set_settings(session_.settings_.host);
        // superkeyboard's rule: the user's engine settings replace the manifest's, field by field.
        const json::Value merged = json::MergeTop(&session_.manifest_.settings, &session_.settings_.engine);
        if (merged.IsObject()) engine_.SetSettings(json::Write(merged));
    }
    if (!started_) {
        started_ = true;
        generation_ = session_.generation_;
        host_.Start(session_.mode_, session_.profile_, field);
        return;
    }
    if (generation_ != session_.generation_) {
        generation_ = session_.generation_;
        host_.Follow(session_.mode_, session_.profile_, field);
    }
}

void Context::After(const ContextInfo& info) {
    if (host_.TakeStateChanged()) {
        session_.Changed(host_.mode(), host_.profile());
        generation_ = session_.generation_;
    }
    host_.TakeCapsWanted();  // the lock can't be set from here (docs/spec.md §5)
    if (session_.sink_ && session_.settings_.publish_state) session_.sink_->Publish(State(info));
}

std::string Context::State(const ContextInfo& info) {
    HostInfo h;
    h.ime = session_.manifest_.name;
    h.version = session_.version_;
    h.frontend = session_.frontend_;
    h.app = info.app;
    h.mode = host_.mode();
    h.profile = host_.profile();
    h.profiles = engine_.HasProfiles();
    h.secure = info.secure;
    h.off = Off(info);
    return PublishedState(host_.EngineState(), h);
}

bool Context::Key(const KeyDown& key, Field& field, const ContextInfo& info) {
    Sync(field);
    bool eaten;
    if (info.secure || Off(info)) {
        // Nothing composes; the language keys still switch mode (docs/spec.md §8).
        eaten = LangKeyOf(key) != LangKey::kNone && LangKeyOf(key) != LangKey::kCapsLock
                    ? host_.Key(key, field)
                    : host_.TypeOnly(key, field);
    } else {
        eaten = host_.Key(key, field);
    }
    After(info);
    return eaten;
}

void Context::KeyUp(const KeyDown& key, Field& field, const ContextInfo& info) {
    Sync(field);
    host_.KeyUp(key, field);
    After(info);
}

void Context::FocusIn(Field& field, const ContextInfo& info) {
    session_.ReloadSettings();
    Sync(field);
    After(info);
}

void Context::FocusOut(Field& field) {
    if (host_.composing()) host_.Flush(field);
}

void Context::SwitchMode(Mode mode, Field& field, const ContextInfo& info) {
    Sync(field);
    host_.SwitchMode(mode, field);
    After(info);
}

void Context::SwitchProfile(const std::string& profile, Field& field, const ContextInfo& info) {
    Sync(field);
    host_.SwitchProfile(profile, field);
    After(info);
}

}  // namespace bl
