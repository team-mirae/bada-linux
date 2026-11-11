// The engine manifest (docs/spec.md §2): superkeyboard's manifest (team-mirae/superkeyboard,
// docs/ime-contract.md §8), so the same fields describe the engine, plus an optional "linux" object
// with what only a Linux input method needs: its permanent id, its language, keyboard layout and
// indicator labels.
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "json.h"

namespace bl {

// One buffer of P_data_new.
struct DataItem {
    enum class Kind { kNull, kFile, kConcat } kind = Kind::kNull;
    std::string file;  // kFile
    struct Part {
        enum class Kind { kFile, kText, kDir } kind = Kind::kFile;
        std::string value;   // a path, or the text
        std::string suffix;  // kDir
    };
    std::vector<Part> parts;  // kConcat
};

struct Manifest {
    std::filesystem::path file;  // where it was read from ("" when parsed from text)
    std::filesystem::path root;  // relative paths start here
    std::string name;
    std::string description;
    std::string library;  // {ext} → so (dylib on macOS, for the core tests), {arch} → x86_64 / aarch64
    std::string prefix;
    std::vector<DataItem> data;  // empty: P_new(NULL)
    bool has_data = false;       // "data" present (possibly []): P_data_new is called
    json::Value settings;        // what every engine starts with (an object, or null)
    std::string mode = "henkan";
    std::string keyboard = "jis";

    // "linux"
    std::string id;               // the input method's name in Fcitx5 and IBus. Permanent. Default bada-<name>
    std::string display_name;     // the name in the input method list; default: name
    std::string language = "ja";  // the language it's listed under
    std::string layout = "jp";    // the XKB layout IBus puts under it
    std::string label_henkan = "\xE3\x81\x82";  // あ
    std::string label_muhenkan = "A";
    std::string icon;  // an icon name or an absolute path; "": input-keyboard
};

std::optional<Manifest> ParseManifest(std::string_view text, const std::filesystem::path& manifest_dir,
                                      std::string* error);
std::optional<Manifest> LoadManifest(const std::filesystem::path& file, std::string* error);

// The engine library's path, placeholders filled in for this build.
std::filesystem::path LibraryPath(const Manifest& manifest);

// The buffers for P_data_new, in order (superkeyboard's rules: a concatenated file part ends with a
// newline, a directory part takes its matching files in name order, each newline-terminated).
bool LoadData(const Manifest& manifest, std::vector<std::string>* buffers, std::string* error);

// An input method id: a lowercase letter, then lowercase letters, digits, - _ . (at most 64).
bool IsImId(std::string_view text);

std::filesystem::path PathFromUtf8(std::string_view utf8);
std::string Utf8Path(const std::filesystem::path& path);
bool ReadFile(const std::filesystem::path& file, std::string* bytes);

}  // namespace bl
