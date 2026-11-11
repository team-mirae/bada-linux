// The user's settings for one IME (docs/spec.md §9): a JSON file, ~/.config/bada-linux/<id>.json,
// read when an input context gains focus if the file changed. Missing or invalid fields take their
// defaults; a missing file is all defaults.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "host.h"
#include "json.h"
#include "manifest.h"

namespace bl {

struct Settings {
    HostSettings host;
    json::Value engine;       // replaces the manifest's settings, top-level field by field
    bool publish_state = true;  // publish the state for other programs (docs/api.md)
    std::vector<std::string> off_in;  // programs nothing composes in
    std::string dictionary;   // a user dictionary for P_dictionary_load; "": the default path
};

// Defaults, adjusted for the manifest (keyboard "us" → pass_on "system").
Settings DefaultSettings(const Manifest& manifest);
Settings ParseSettings(std::string_view text, const Manifest& manifest);

// $XDG_CONFIG_HOME/bada-linux/<id>.json (else ~/.config/…).
std::filesystem::path SettingsPath(const Manifest& manifest);
// $XDG_DATA_HOME/bada-linux/<id>/dictionary.json (else ~/.local/share/…).
std::filesystem::path DictionaryPath(const Manifest& manifest);

// Reads the settings file when it changed since the last call (its modification time and size).
class SettingsFile {
public:
    explicit SettingsFile(const Manifest& manifest) : manifest_(manifest), path_(SettingsPath(manifest)) {}
    // True when `out` was (re)read; the first call always reads.
    bool Reload(Settings* out);
    const std::filesystem::path& path() const { return path_; }

private:
    const Manifest& manifest_;
    std::filesystem::path path_;
    bool read_ = false;
    bool exists_ = false;
    std::filesystem::file_time_type mtime_{};
    std::uintmax_t size_ = 0;
};

}  // namespace bl
