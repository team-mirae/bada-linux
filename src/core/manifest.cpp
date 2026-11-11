#include "manifest.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iterator>

namespace bl {
namespace fs = std::filesystem;

namespace {

bool Fail(std::string* error, std::string message) {
    if (error) *error = std::move(message);
    return false;
}

// ${VAR} from the environment.
std::string Expand(const std::string& text) {
    std::string out;
    for (size_t i = 0; i < text.size();) {
        if (text.compare(i, 2, "${") == 0) {
            const size_t end = text.find('}', i);
            if (end != std::string::npos) {
                const std::string name = text.substr(i + 2, end - i - 2);
                if (const char* value = std::getenv(name.c_str())) out += value;
                i = end + 1;
                continue;
            }
        }
        out += text[i++];
    }
    return out;
}

void Replace(std::string* text, std::string_view from, std::string_view to) {
    for (size_t at = text->find(from); at != std::string::npos; at = text->find(from, at + to.size())) {
        text->replace(at, from.size(), to);
    }
}

fs::path Resolve(const Manifest& m, const std::string& path) {
    const fs::path p = PathFromUtf8(path);
    return p.is_absolute() ? p : m.root / p;
}

bool ParsePart(const json::Value& v, DataItem::Part* part) {
    if (const std::string* s = v.String()) {
        *part = {DataItem::Part::Kind::kFile, *s, {}};
        return true;
    }
    if (!v.IsObject()) return false;
    if (const json::Value* f = v.Get("file"); f && f->String()) {
        *part = {DataItem::Part::Kind::kFile, *f->String(), {}};
    } else if (const json::Value* t = v.Get("text"); t && t->String()) {
        *part = {DataItem::Part::Kind::kText, *t->String(), {}};
    } else if (const json::Value* d = v.Get("dir"); d && d->String()) {
        *part = {DataItem::Part::Kind::kDir, *d->String(), v.GetString("suffix")};
    } else {
        return false;
    }
    return true;
}

bool ParseItem(const json::Value& v, DataItem* item) {
    if (v.IsNull()) {
        item->kind = DataItem::Kind::kNull;
        return true;
    }
    if (const std::string* s = v.String()) {
        item->kind = DataItem::Kind::kFile;
        item->file = *s;
        return true;
    }
    if (!v.IsObject()) return false;
    if (const json::Value* f = v.Get("file"); f && f->String()) {
        item->kind = DataItem::Kind::kFile;
        item->file = *f->String();
        return true;
    }
    const json::Value* parts = v.Get("concat");
    if (!parts || !parts->IsArray()) return false;
    item->kind = DataItem::Kind::kConcat;
    for (const json::Value& p : parts->array) {
        DataItem::Part part;
        if (!ParsePart(p, &part)) return false;
        item->parts.push_back(std::move(part));
    }
    return true;
}

void EndLine(std::string* buffer) {
    if (!buffer->empty() && buffer->back() != '\n') buffer->push_back('\n');
}

}  // namespace

fs::path PathFromUtf8(std::string_view utf8) {
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
}

std::string Utf8Path(const fs::path& path) {
    const std::u8string s = path.u8string();
    return std::string(s.begin(), s.end());
}

bool ReadFile(const fs::path& file, std::string* bytes) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return false;
    bytes->assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return !in.bad();
}

bool IsImId(std::string_view s) {
    if (s.empty() || s.size() > 64 || s.front() < 'a' || s.front() > 'z') return false;
    for (char ch : s) {
        const bool ok = (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.';
        if (!ok) return false;
    }
    return true;
}

std::optional<Manifest> ParseManifest(std::string_view text, const fs::path& manifest_dir, std::string* error) {
    const auto doc = json::Parse(text);
    if (!doc || !doc->IsObject()) {
        Fail(error, "the manifest isn't a JSON object");
        return std::nullopt;
    }
    const json::Value& v = *doc;
    Manifest m;
    m.name = v.GetString("name");
    m.library = v.GetString("library");
    m.prefix = v.GetString("prefix");
    if (m.name.empty() || m.library.empty() || m.prefix.empty()) {
        Fail(error, "the manifest needs name, library and prefix");
        return std::nullopt;
    }
    m.description = v.GetString("description");
    const fs::path root = PathFromUtf8(Expand(v.GetString("root")));
    m.root = root.empty() ? manifest_dir : root.is_absolute() ? root : manifest_dir / root;
    m.mode = v.GetString("mode", "henkan");
    if (m.mode != "henkan" && m.mode != "muhenkan") {
        Fail(error, "mode is \"henkan\" or \"muhenkan\"");
        return std::nullopt;
    }
    m.keyboard = v.GetString("keyboard", "jis");
    if (const json::Value* s = v.Get("settings")) m.settings = *s;
    if (const json::Value* data = v.Get("data")) {
        if (!data->IsArray() || data->array.size() > 16) {
            Fail(error, "data is a list of at most 16 buffers");
            return std::nullopt;
        }
        m.has_data = true;
        for (const json::Value& item : data->array) {
            DataItem d;
            if (!ParseItem(item, &d)) {
                Fail(error, "a data item is a path, null, {\"file\"} or {\"concat\": [...]}");
                return std::nullopt;
            }
            m.data.push_back(std::move(d));
        }
    }
    const json::Value none;
    const json::Value* l = v.Get("linux");
    if (l && !l->IsObject()) {
        Fail(error, "\"linux\" is an object");
        return std::nullopt;
    }
    if (!l) l = &none;
    m.id = l->GetString("id", "bada-" + m.name);
    if (!IsImId(m.id)) {
        Fail(error, "linux.id (default bada-<name>) is a lowercase letter, then letters, digits, - _ . (at most 64)");
        return std::nullopt;
    }
    m.display_name = l->GetString("display_name", m.name);
    m.language = l->GetString("language", m.language);
    m.layout = l->GetString("layout", m.layout);
    if (const json::Value* labels = l->Get("labels"); labels && labels->IsObject()) {
        m.label_henkan = labels->GetString("henkan", m.label_henkan);
        m.label_muhenkan = labels->GetString("muhenkan", m.label_muhenkan);
    }
    m.icon = l->GetString("icon");
    return m;
}

std::optional<Manifest> LoadManifest(const fs::path& file, std::string* error) {
    std::string text;
    if (!ReadFile(file, &text)) {
        Fail(error, "can't read " + Utf8Path(file));
        return std::nullopt;
    }
    auto m = ParseManifest(text, file.parent_path(), error);
    if (m) m->file = file;
    return m;
}

fs::path LibraryPath(const Manifest& m) {
    std::string library = m.library;
#if defined(__APPLE__)
    Replace(&library, "{ext}", "dylib");
#else
    Replace(&library, "{ext}", "so");
#endif
#if defined(__aarch64__)
    Replace(&library, "{arch}", "aarch64");
#elif defined(__x86_64__)
    Replace(&library, "{arch}", "x86_64");
#elif defined(__i386__)
    Replace(&library, "{arch}", "i686");
#endif
    return Resolve(m, library);
}

bool LoadData(const Manifest& m, std::vector<std::string>* buffers, std::string* error) {
    buffers->clear();
    for (const DataItem& item : m.data) {
        std::string buffer;
        if (item.kind == DataItem::Kind::kFile) {
            if (!ReadFile(Resolve(m, item.file), &buffer)) return Fail(error, "can't read " + item.file);
        } else if (item.kind == DataItem::Kind::kConcat) {
            for (const DataItem::Part& part : item.parts) {
                if (part.kind == DataItem::Part::Kind::kText) {
                    buffer += part.value;
                } else if (part.kind == DataItem::Part::Kind::kFile) {
                    std::string bytes;
                    if (!ReadFile(Resolve(m, part.value), &bytes)) return Fail(error, "can't read " + part.value);
                    buffer += bytes;
                    EndLine(&buffer);
                } else {
                    std::vector<fs::path> files;
                    std::error_code ec;
                    for (const auto& entry : fs::directory_iterator(Resolve(m, part.value), ec)) {
                        const std::u8string name = entry.path().filename().u8string();
                        const std::string utf8(name.begin(), name.end());
                        if (entry.is_regular_file() && utf8.size() >= part.suffix.size() &&
                            utf8.compare(utf8.size() - part.suffix.size(), part.suffix.size(), part.suffix) == 0) {
                            files.push_back(entry.path());
                        }
                    }
                    if (ec) return Fail(error, "can't list " + part.value);
                    std::sort(files.begin(), files.end());
                    for (const fs::path& f : files) {
                        std::string bytes;
                        if (!ReadFile(f, &bytes)) return Fail(error, "can't read " + Utf8Path(f));
                        buffer += bytes;
                        EndLine(&buffer);
                    }
                }
            }
        }
        buffers->push_back(std::move(buffer));
    }
    return true;
}

}  // namespace bl
