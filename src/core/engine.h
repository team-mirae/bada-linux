// A bada-compatible v1 engine, loaded at run time (bada-compatible docs/bada-compatible-v1.md §3):
// dlopen. The required functions must all be
// there; each optional one is looked up and done without when missing, as the contract asks.
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "keys.h"
#include "manifest.h"
#include "output.h"

namespace bl {

// The engine library and its shared data: one per process, shared by every engine (spec §4 there:
// Data is safe to share across threads; each engine is used by one thread at a time).
class Module {
public:
    ~Module();
    Module(const Module&) = delete;
    Module& operator=(const Module&) = delete;

    // Loads the library, checks the required functions, builds the data from the manifest's
    // buffers. nullptr and `error` on failure.
    static std::unique_ptr<Module> Load(const Manifest& manifest, std::string* error);

    struct Api {
        void* (*new_)(const void* data) = nullptr;
        void (*free_)(void* engine) = nullptr;
        const char* (*key)(void* engine, const char* event) = nullptr;
        const char* (*set_mode)(void* engine, std::uint32_t mode) = nullptr;
        const char* (*flush)(void* engine) = nullptr;
        void* data_new = nullptr;  // N (pointer, length) pairs; called through CallDataNew
        void (*data_free)(const void* data) = nullptr;
        const char* (*set_settings)(void* engine, const char* json) = nullptr;
        const char* (*set_profile)(void* engine, const char* name) = nullptr;
        const char* (*host_selection)(void* engine, const char* text) = nullptr;
        const char* (*catalog)(void* engine) = nullptr;
        char* (*dictionary_load)(const char* json) = nullptr;
        void (*string_free)(char* text) = nullptr;
        const char* (*state)(void* engine) = nullptr;  // v1.1
    };
    const Api& api() const { return api_; }
    const void* data() const { return data_; }
    const Manifest& manifest() const { return manifest_; }

    // P_dictionary_load (process-wide). Returns the summary, or "" when the engine has none.
    std::string LoadDictionary(const std::string& json) const;

private:
    Module() = default;
    void* library_ = nullptr;
    Api api_;
    const void* data_ = nullptr;
    Manifest manifest_;
};

enum class Mode { kMuhenkan = 0, kHenkan = 1 };

// One engine (one per input context).
class Engine {
public:
    explicit Engine(const Module& module);
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    bool ok() const { return engine_ != nullptr; }
    Output Key(const std::string& code, const Mods& mods);
    Output SetMode(Mode mode);
    Output Flush();
    bool HasProfiles() const { return api_.set_profile != nullptr; }
    Output SetProfile(const std::string& name);  // a flush when the engine has no profiles
    bool HasHostSelection() const { return api_.host_selection != nullptr; }
    Output HostSelection(const std::string& text);
    // The settings in effect, as JSON ("" without P_set_settings).
    std::string SetSettings(const std::string& json);
    std::string Catalog();  // "{}" without P_catalog
    bool HasState() const { return api_.state != nullptr; }
    std::string State();  // P_state's JSON (v1.1); "" without it

private:
    const Module::Api& api_;
    void* engine_ = nullptr;
};

// {"key": code, "shift": …, "ctrl": …, "alt": …, "meta": …}
std::string KeyEventJson(const std::string& code, const Mods& mods);

}  // namespace bl
