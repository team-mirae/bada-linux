#include "engine.h"

#include <utility>

#include "json.h"

#include <dlfcn.h>

namespace bl {
namespace {

void* OpenLibrary(const std::filesystem::path& path, std::string* error) {
    void* module = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!module) *error = dlerror();
    return module;
}

void* Symbol(void* library, const std::string& name) { return dlsym(library, name.c_str()); }

void CloseLibrary(void* library) { dlclose(library); }

template <typename F>
void Find(void* library, const std::string& prefix, const char* name, F* out) {
    *out = reinterpret_cast<F>(Symbol(library, prefix + "_" + name));
}

// P_data_new with N buffers, as (pointer, length) pairs: argument 2k is a pointer, 2k+1 a length.
template <size_t I>
using DataArg = std::conditional_t<I % 2 == 0, const std::uint8_t*, size_t>;

template <size_t I>
DataArg<I> ArgAt(const std::vector<std::string>& buffers) {
    const std::string& b = buffers[I / 2];
    if constexpr (I % 2 == 0) {
        return b.empty() ? nullptr : reinterpret_cast<const std::uint8_t*>(b.data());
    } else {
        return b.size();
    }
}

template <size_t... I>
const void* CallDataNewWith(void* fn, const std::vector<std::string>& buffers, std::index_sequence<I...>) {
    using Fn = const void* (*)(DataArg<I>...);
    return reinterpret_cast<Fn>(fn)(ArgAt<I>(buffers)...);
}

template <size_t N>
const void* CallDataNewN(void* fn, const std::vector<std::string>& buffers) {
    return CallDataNewWith(fn, buffers, std::make_index_sequence<2 * N>());
}

const void* CallDataNew(void* fn, const std::vector<std::string>& b) {
    switch (b.size()) {
        case 0: return reinterpret_cast<const void* (*)()>(fn)();
        case 1: return CallDataNewN<1>(fn, b);
        case 2: return CallDataNewN<2>(fn, b);
        case 3: return CallDataNewN<3>(fn, b);
        case 4: return CallDataNewN<4>(fn, b);
        case 5: return CallDataNewN<5>(fn, b);
        case 6: return CallDataNewN<6>(fn, b);
        case 7: return CallDataNewN<7>(fn, b);
        case 8: return CallDataNewN<8>(fn, b);
        case 9: return CallDataNewN<9>(fn, b);
        case 10: return CallDataNewN<10>(fn, b);
        case 11: return CallDataNewN<11>(fn, b);
        case 12: return CallDataNewN<12>(fn, b);
        case 13: return CallDataNewN<13>(fn, b);
        case 14: return CallDataNewN<14>(fn, b);
        case 15: return CallDataNewN<15>(fn, b);
        case 16: return CallDataNewN<16>(fn, b);
    }
    return nullptr;
}

Output Parsed(const char* reply) { return reply ? ParseOutput(reply) : Output{}; }

}  // namespace

std::unique_ptr<Module> Module::Load(const Manifest& manifest, std::string* error) {
    std::string ignored;
    if (!error) error = &ignored;
    std::unique_ptr<Module> m(new Module());
    m->manifest_ = manifest;
    m->library_ = OpenLibrary(LibraryPath(manifest), error);
    if (!m->library_) return nullptr;
    Api& a = m->api_;
    const std::string& p = manifest.prefix;
    void* library = m->library_;
    Find(library, p, "new", &a.new_);
    Find(library, p, "free", &a.free_);
    Find(library, p, "key", &a.key);
    Find(library, p, "set_mode", &a.set_mode);
    Find(library, p, "flush", &a.flush);
    if (!a.new_ || !a.free_ || !a.key || !a.set_mode || !a.flush) {
        *error = "the library lacks a required function (" + p + "_new, _free, _key, _set_mode, _flush)";
        return nullptr;
    }
    a.data_new = Symbol(library, p + "_data_new");
    Find(library, p, "data_free", &a.data_free);
    Find(library, p, "set_settings", &a.set_settings);
    Find(library, p, "set_profile", &a.set_profile);
    Find(library, p, "host_selection", &a.host_selection);
    Find(library, p, "catalog", &a.catalog);
    Find(library, p, "dictionary_load", &a.dictionary_load);
    Find(library, p, "string_free", &a.string_free);
    Find(library, p, "state", &a.state);
    if (manifest.has_data) {
        if (!a.data_new) {
            *error = "the manifest lists data, but the library has no " + p + "_data_new";
            return nullptr;
        }
        std::vector<std::string> buffers;
        if (!LoadData(manifest, &buffers, error)) return nullptr;
        m->data_ = CallDataNew(a.data_new, buffers);
        if (!m->data_) {
            *error = p + "_data_new failed";
            return nullptr;
        }
    }
    return m;
}

Module::~Module() {
    if (data_ && api_.data_free) api_.data_free(data_);
    if (library_) CloseLibrary(library_);
}

std::string Module::LoadDictionary(const std::string& text) const {
    if (!api_.dictionary_load) return {};
    char* summary = api_.dictionary_load(text.c_str());
    if (!summary) return {};
    std::string out = summary;
    if (api_.string_free) api_.string_free(summary);
    return out;
}

std::string KeyEventJson(const std::string& code, const Mods& mods) {
    std::string out = "{\"key\":" + json::Quote(code);
    out += mods.shift ? ",\"shift\":true" : ",\"shift\":false";
    out += mods.ctrl ? ",\"ctrl\":true" : ",\"ctrl\":false";
    out += mods.alt ? ",\"alt\":true" : ",\"alt\":false";
    out += mods.meta ? ",\"meta\":true}" : ",\"meta\":false}";
    return out;
}

Engine::Engine(const Module& module) : api_(module.api()), engine_(api_.new_(module.data())) {}

Engine::~Engine() {
    if (engine_) api_.free_(engine_);
}

Output Engine::Key(const std::string& code, const Mods& mods) {
    if (!engine_) return {};
    return Parsed(api_.key(engine_, KeyEventJson(code, mods).c_str()));
}

Output Engine::SetMode(Mode mode) {
    if (!engine_) return {};
    return Parsed(api_.set_mode(engine_, static_cast<std::uint32_t>(mode)));
}

Output Engine::Flush() {
    if (!engine_) return {};
    return Parsed(api_.flush(engine_));
}

Output Engine::SetProfile(const std::string& name) {
    if (!engine_) return {};
    if (!api_.set_profile) return Flush();
    return Parsed(api_.set_profile(engine_, name.c_str()));
}

Output Engine::HostSelection(const std::string& text) {
    if (!engine_ || !api_.host_selection) return {};
    return Parsed(api_.host_selection(engine_, text.c_str()));
}

std::string Engine::SetSettings(const std::string& text) {
    if (!engine_ || !api_.set_settings) return {};
    const char* reply = api_.set_settings(engine_, text.c_str());
    return reply ? reply : "";
}

std::string Engine::State() {
    if (!engine_ || !api_.state) return {};
    const char* reply = api_.state(engine_);
    return reply ? reply : "";
}

std::string Engine::Catalog() {
    if (!engine_ || !api_.catalog) return "{}";
    const char* reply = api_.catalog(engine_);
    return reply ? reply : "{}";
}

}  // namespace bl
