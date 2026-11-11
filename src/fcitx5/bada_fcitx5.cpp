// The Fcitx5 frontend (docs/spec.md §1): one addon, bada-linux, that lists one input method per
// installed manifest. Each input method holds both modes (§3); the session layer (src/core/session.h)
// does everything but moving keys in and text out, which is this file's.
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include <fcitx-utils/capabilityflags.h>
#include <fcitx-utils/key.h>
#include <fcitx-utils/log.h>
#include <fcitx/addonfactory.h>
#include <fcitx/addoninstance.h>
#include <fcitx/addonmanager.h>
#include <fcitx/candidatelist.h>
#include <fcitx/event.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>
#include <fcitx/inputcontextproperty.h>
#include <fcitx/inputmethodengine.h>
#include <fcitx/inputmethodentry.h>
#include <fcitx/inputpanel.h>
#include <fcitx/instance.h>
#include <fcitx/text.h>
#include <fcitx/userinterface.h>

#include "discover.h"
#include "publish.h"
#include "session.h"

#ifndef BADA_LINUX_VERSION
#define BADA_LINUX_VERSION "0.0.0"
#endif

namespace {

constexpr const char* kAddon = "bada-linux";

// A menu row. The contract has no "pick a row": choosing one does nothing (docs/spec.md §7).
class Row final : public fcitx::CandidateWord {
public:
    Row(std::string text, std::string note) : CandidateWord(fcitx::Text(std::move(text))) {
        if (!note.empty()) setComment(fcitx::Text(std::move(note)));
    }
    void select(fcitx::InputContext*) const override {}
};

// An input context as the host's text field (bada-compatible §7.1).
class Field final : public bl::Field {
public:
    explicit Field(fcitx::InputContext* ic) : ic_(ic) {}

    void Apply(const bl::Output& out) override {
        if (!out.commit.empty()) {
            if (out.replace_selection) DeleteSelection();
            ic_->commitString(out.commit);
        }
        fcitx::Text preedit;
        if (!out.preedit.empty()) {
            // Underlined; highlighted too while the menu shows an entry other than entry 0.
            fcitx::TextFormatFlags flags = fcitx::TextFormatFlag::Underline;
            if (!out.menu.empty() && out.selected != out.zero) flags |= fcitx::TextFormatFlag::HighLight;
            preedit.append(out.preedit, flags);
            preedit.setCursor(static_cast<int>(out.preedit.size()));
        }
        auto& panel = ic_->inputPanel();
        if (ic_->capabilityFlags().test(fcitx::CapabilityFlag::Preedit)) {
            panel.setClientPreedit(preedit);
            panel.setPreedit(fcitx::Text());
        } else {
            panel.setPreedit(preedit);
        }
        panel.setCandidateList(Menu(out));
        ic_->updatePreedit();
        ic_->updateUserInterface(fcitx::UserInterfaceComponent::InputPanel);
    }

    std::string Selection() override {
        if (!ic_->capabilityFlags().test(fcitx::CapabilityFlag::SurroundingText)) return {};
        const auto& s = ic_->surroundingText();
        return s.isValid() ? s.selectedText() : std::string();
    }

    void Type(const std::string& text) override { ic_->commitString(text); }

private:
    void DeleteSelection() {
        if (!ic_->capabilityFlags().test(fcitx::CapabilityFlag::SurroundingText)) return;
        const auto& s = ic_->surroundingText();
        if (!s.isValid() || s.anchor() == s.cursor()) return;
        const int from = static_cast<int>(std::min(s.anchor(), s.cursor()));
        const int to = static_cast<int>(std::max(s.anchor(), s.cursor()));
        ic_->deleteSurroundingText(from - static_cast<int>(s.cursor()), static_cast<unsigned>(to - from));
    }

    static std::unique_ptr<fcitx::CandidateList> Menu(const bl::Output& out) {
        if (out.menu.empty()) return nullptr;
        auto list = std::make_unique<fcitx::CommonCandidateList>();
        list->setPageSize(static_cast<int>(out.menu.size()));
        // Rows above entry 0 are labelled with their entries (−2, −1); the rest have none.
        std::vector<std::string> labels;
        for (size_t row = 0; row < out.menu.size(); ++row) {
            labels.push_back(row < out.zero ? "\xE2\x88\x92" + std::to_string(out.zero - row) + " " : "");
        }
        list->setLabels(labels);
        if (out.horizontal) list->setLayoutHint(fcitx::CandidateLayoutHint::Horizontal);
        for (size_t row = 0; row < out.menu.size(); ++row) {
            list->append<Row>(out.menu[row], row < out.notes.size() ? out.notes[row] : std::string());
        }
        if (out.selected < out.menu.size()) list->setGlobalCursorIndex(static_cast<int>(out.selected));
        return list;
    }

    fcitx::InputContext* ic_;
};

bl::ContextInfo InfoOf(fcitx::InputContext* ic) {
    bl::ContextInfo info;
    info.app = ic->program();
    const auto caps = ic->capabilityFlags();
    info.secure = caps.test(fcitx::CapabilityFlag::Password) || caps.test(fcitx::CapabilityFlag::Sensitive);
    return info;
}

bl::KeyDown KeyOf(const fcitx::KeyEvent& event) {
    const fcitx::Key& raw = event.rawKey();
    bl::KeyDown k;
    k.code = raw.code() > 8 ? static_cast<unsigned>(raw.code() - 8) : 0;  // XKB keycode → evdev
    k.sym = static_cast<unsigned>(raw.sym());
    const fcitx::KeyStates s = raw.states();
    k.mods.shift = s.test(fcitx::KeyState::Shift);
    k.mods.ctrl = s.test(fcitx::KeyState::Ctrl);
    k.mods.alt = s.test(fcitx::KeyState::Alt);
    k.mods.meta = s.test(fcitx::KeyState::Super) || s.test(fcitx::KeyState::Super2);
    k.caps = s.test(fcitx::KeyState::CapsLock);
    return k;
}

class Engine;

// One IME: its manifest, and its session once loaded.
struct Ime {
    bl::Manifest manifest;
    std::unique_ptr<bl::Publisher> publisher;
    std::unique_ptr<bl::Session> session;
    bool failed = false;
};

// Per input context: one Context per IME used there, and the keys held down (auto-repeat).
class State final : public fcitx::InputContextProperty {
public:
    std::unordered_map<std::string, std::unique_ptr<bl::Context>> contexts;
    std::set<unsigned> down;
};

class Engine final : public fcitx::InputMethodEngineV2 {
public:
    explicit Engine(fcitx::Instance* instance)
        : instance_(instance), factory_([](fcitx::InputContext&) { return new State(); }) {
        std::vector<std::string> errors;
        for (bl::Manifest& m : bl::DiscoverManifests(&errors)) {
            auto ime = std::make_unique<Ime>();
            ime->manifest = std::move(m);
            imes_.push_back(std::move(ime));
        }
        for (const std::string& e : errors) FCITX_ERROR() << "bada-linux: " << e;
        instance_->inputContextManager().registerProperty("badaLinuxState", &factory_);
    }

    std::vector<fcitx::InputMethodEntry> listInputMethods() override {
        std::vector<fcitx::InputMethodEntry> out;
        for (const auto& ime : imes_) {
            const bl::Manifest& m = ime->manifest;
            fcitx::InputMethodEntry entry(m.id, m.display_name, m.language, kAddon);
            entry.setIcon(m.icon.empty() ? "input-keyboard" : m.icon);
            entry.setLabel(m.label_henkan);
            entry.setNativeName(m.display_name);
            entry.setConfigurable(false);
            out.push_back(std::move(entry));
        }
        return out;
    }

    void keyEvent(const fcitx::InputMethodEntry& entry, fcitx::KeyEvent& event) override {
        auto* ic = event.inputContext();
        bl::Context* ctx = ContextFor(entry, ic);
        if (!ctx) return;
        State* state = ic->propertyFor(&factory_);
        bl::KeyDown key = KeyOf(event);
        Field field(ic);
        const bl::ContextInfo info = InfoOf(ic);
        const bl::Mode before = SessionOf(entry)->mode();
        if (event.isRelease()) {
            state->down.erase(key.code);
            ctx->KeyUp(key, field, info);
        } else {
            key.repeat = key.code && !state->down.insert(key.code).second;
            if (ctx->Key(key, field, info)) event.filterAndAccept();
        }
        if (SessionOf(entry)->mode() != before) ModeChanged(ic);
    }

    void activate(const fcitx::InputMethodEntry& entry, fcitx::InputContextEvent& event) override {
        auto* ic = event.inputContext();
        if (bl::Context* ctx = ContextFor(entry, ic)) {
            Field field(ic);
            ctx->FocusIn(field, InfoOf(ic));
        }
    }

    void deactivate(const fcitx::InputMethodEntry& entry, fcitx::InputContextEvent& event) override {
        reset(entry, event);
    }

    // Focus out, a click in the text, the app's reset: commit what's staged (bada-compatible §7.6).
    void reset(const fcitx::InputMethodEntry& entry, fcitx::InputContextEvent& event) override {
        auto* ic = event.inputContext();
        State* state = ic->propertyFor(&factory_);
        state->down.clear();
        const auto it = state->contexts.find(entry.uniqueName());
        if (it == state->contexts.end()) return;
        Field field(ic);
        it->second->FocusOut(field);
        ic->inputPanel().reset();
        ic->updatePreedit();
        ic->updateUserInterface(fcitx::UserInterfaceComponent::InputPanel);
    }

    std::string subMode(const fcitx::InputMethodEntry& entry, fcitx::InputContext&) override {
        bl::Session* s = SessionOf(entry);
        if (!s) return {};
        return s->mode() == bl::Mode::kHenkan ? "henkan" : "muhenkan";
    }

    std::string subModeLabelImpl(const fcitx::InputMethodEntry& entry, fcitx::InputContext&) override {
        bl::Session* s = SessionOf(entry);
        return s ? s->Label() : std::string();
    }

    std::string subModeIconImpl(const fcitx::InputMethodEntry& entry, fcitx::InputContext&) override {
        return entry.icon();
    }

private:
    Ime* ImeOf(const fcitx::InputMethodEntry& entry) {
        for (auto& ime : imes_) {
            if (ime->manifest.id == entry.uniqueName()) return ime.get();
        }
        return nullptr;
    }

    bl::Session* SessionOf(const fcitx::InputMethodEntry& entry) {
        Ime* ime = ImeOf(entry);
        if (!ime || ime->failed) return nullptr;
        if (!ime->session) {
            ime->publisher = std::make_unique<bl::Publisher>(ime->manifest.id);
            std::string error;
            ime->session = bl::Session::Load(ime->manifest, "fcitx5", BADA_LINUX_VERSION, ime->publisher.get(), &error);
            if (!ime->session) {
                FCITX_ERROR() << "bada-linux: " << ime->manifest.id << ": " << error;
                ime->failed = true;
                return nullptr;
            }
        }
        return ime->session.get();
    }

    bl::Context* ContextFor(const fcitx::InputMethodEntry& entry, fcitx::InputContext* ic) {
        bl::Session* session = SessionOf(entry);
        if (!session) return nullptr;
        State* state = ic->propertyFor(&factory_);
        auto& slot = state->contexts[entry.uniqueName()];
        if (!slot) {
            slot = std::make_unique<bl::Context>(*session);
            if (!slot->ok()) {
                FCITX_ERROR() << "bada-linux: " << entry.uniqueName() << ": the engine didn't start";
                state->contexts.erase(entry.uniqueName());
                return nullptr;
            }
        }
        return slot.get();
    }

    void ModeChanged(fcitx::InputContext* ic) {
        ic->updateUserInterface(fcitx::UserInterfaceComponent::StatusArea);
        instance_->showInputMethodInformation(ic);
    }

    fcitx::Instance* instance_;
    std::vector<std::unique_ptr<Ime>> imes_;
    // After imes_: destroyed first, so every Context goes before its Session.
    fcitx::FactoryFor<State> factory_;
};

class Factory final : public fcitx::AddonFactory {
public:
    fcitx::AddonInstance* create(fcitx::AddonManager* manager) override {
        return new Engine(manager->instance());
    }
};

}  // namespace

FCITX_ADDON_FACTORY(Factory);
