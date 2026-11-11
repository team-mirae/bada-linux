#include "settings.h"

#include <cstdlib>

namespace bl {
namespace fs = std::filesystem;

namespace {

fs::path XdgDir(const char* variable, const char* fallback) {
    if (const char* v = std::getenv(variable); v && *v == '/') return v;
    const char* home = std::getenv("HOME");
    return fs::path(home && *home ? home : "/") / fallback;
}

}  // namespace

Settings DefaultSettings(const Manifest& manifest) {
    Settings s;
    if (manifest.keyboard == "us") s.host.pass_on = PassOn::kSystem;
    return s;
}

Settings ParseSettings(std::string_view text, const Manifest& manifest) {
    Settings s = DefaultSettings(manifest);
    const auto doc = json::Parse(text);
    if (!doc || !doc->IsObject()) return s;
    const json::Value& v = *doc;
    const std::string caps = v.GetString("caps_lock");
    if (caps == "toggle") s.host.caps = CapsSetting::kToggle;
    else if (caps == "transient") s.host.caps = CapsSetting::kTransient;
    if (v.GetString("caps_rule") == "uppersnake") s.host.caps_rule = CapsRule::kUpperSnake;
    const std::string pass_on = v.GetString("pass_on");
    if (pass_on == "jis") s.host.pass_on = PassOn::kJis;
    else if (pass_on == "system") s.host.pass_on = PassOn::kSystem;
    if (const json::Value* kana = v.Get("kana_key")) {
        if (const std::string* p = kana->String()) s.host.kana_profile = *p == "none" ? "" : *p;
    }
    if (const json::Value* ralt = v.Get("right_alt_tap")) {
        if (const std::string* p = ralt->String()) s.host.ralt_profile = *p == "none" ? "" : *p;
    }
    if (const json::Value* engine = v.Get("engine_settings"); engine && engine->IsObject()) s.engine = *engine;
    s.publish_state = v.GetBool("publish_state", true);
    if (const json::Value* off = v.Get("off_in"); off && off->IsArray()) {
        for (const json::Value& p : off->array) {
            if (const std::string* name = p.String()) s.off_in.push_back(*name);
        }
    }
    s.dictionary = v.GetString("dictionary");
    return s;
}

fs::path SettingsPath(const Manifest& manifest) {
    return XdgDir("XDG_CONFIG_HOME", ".config") / "bada-linux" / (manifest.id + ".json");
}

fs::path DictionaryPath(const Manifest& manifest) {
    return XdgDir("XDG_DATA_HOME", ".local/share") / "bada-linux" / manifest.id / "dictionary.json";
}

bool SettingsFile::Reload(Settings* out) {
    std::error_code ec;
    auto mtime = fs::last_write_time(path_, ec);
    const bool exists = !ec;
    if (!exists) mtime = {};
    const std::uintmax_t size = exists ? fs::file_size(path_, ec) : 0;
    if (read_ && exists == exists_ && mtime == mtime_ && size == size_) return false;
    read_ = true;
    exists_ = exists;
    mtime_ = mtime;
    size_ = size;
    std::string text;
    *out = exists && ReadFile(path_, &text) ? ParseSettings(text, manifest_) : DefaultSettings(manifest_);
    return true;
}

}  // namespace bl
