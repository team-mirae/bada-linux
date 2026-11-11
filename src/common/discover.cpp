#include "discover.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <set>

namespace bl {
namespace fs = std::filesystem;

namespace {

std::vector<std::string> Split(const std::string& text) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= text.size()) {
        const size_t end = std::min(text.find(':', start), text.size());
        if (end > start) out.push_back(text.substr(start, end - start));
        start = end + 1;
    }
    return out;
}

std::string Env(const char* name) {
    const char* v = std::getenv(name);
    return v ? v : "";
}

}  // namespace

std::vector<Manifest> DiscoverManifests(std::vector<std::string>* errors) {
    std::vector<fs::path> roots;
    for (const std::string& dir : Split(Env("BADA_LINUX_IMES"))) roots.emplace_back(dir);
    std::string data_home = Env("XDG_DATA_HOME");
    if (data_home.empty() && !Env("HOME").empty()) data_home = Env("HOME") + "/.local/share";
    if (!data_home.empty()) roots.push_back(fs::path(data_home) / "bada-linux" / "imes");
    std::string data_dirs = Env("XDG_DATA_DIRS");
    if (data_dirs.empty()) data_dirs = "/usr/local/share:/usr/share";
    for (const std::string& dir : Split(data_dirs)) roots.push_back(fs::path(dir) / "bada-linux" / "imes");

    std::vector<Manifest> found;
    std::set<std::string> ids;
    for (const fs::path& root : roots) {
        std::error_code ec;
        std::vector<fs::path> dirs;
        for (const auto& entry : fs::directory_iterator(root, ec)) {
            if (entry.is_directory()) dirs.push_back(entry.path());
        }
        std::sort(dirs.begin(), dirs.end());
        for (const fs::path& dir : dirs) {
            const fs::path file = dir / "ime.json";
            if (!fs::exists(file, ec)) continue;
            std::string error;
            auto m = LoadManifest(file, &error);
            if (!m) {
                if (errors) errors->push_back(file.string() + ": " + error);
                continue;
            }
            if (ids.insert(m->id).second) found.push_back(std::move(*m));
        }
    }
    return found;
}

}  // namespace bl
