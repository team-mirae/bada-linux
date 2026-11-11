// The IBus frontend (docs/spec.md §1): bada-ibus, one process per IME, started by ibus-daemon from
// the component XML that `bada-ibus --xml` writes. IBus makes one engine object per input context;
// each holds a Context of the process's one Session (src/core/session.h), which does everything but
// moving keys in and text out.
//
//   bada-ibus --manifest <ime.json> [--ibus]     run (--ibus: started by ibus-daemon)
//   bada-ibus --xml --manifest <ime.json> [--exec <path>]   the component XML, on stdout
#include <cstdio>
#include <cstring>
#include <memory>
#include <set>
#include <string>

#include <ibus.h>

#include "json.h"
#include "publish.h"
#include "session.h"

#ifndef BADA_LINUX_VERSION
#define BADA_LINUX_VERSION "0.0.0"
#endif

namespace {

bl::Session* g_session = nullptr;

// --- the engine object

struct BadaEngine {
    IBusEngine parent;
    bl::Context* context;
    std::set<unsigned>* down;  // keys held (auto-repeat)
    IBusPropList* props;
    IBusProperty* mode_prop;
    gboolean secure;
};

struct BadaEngineClass {
    IBusEngineClass parent;
};

GType bada_engine_get_type();
#define BADA_ENGINE(o) (reinterpret_cast<BadaEngine*>(o))

G_DEFINE_TYPE(BadaEngine, bada_engine, IBUS_TYPE_ENGINE)

// UTF-8 → the number of characters.
guint Chars(const std::string& s) { return static_cast<guint>(g_utf8_strlen(s.c_str(), static_cast<gssize>(s.size()))); }

// The input context as the host's text field (bada-compatible §7.1).
//
// The preedit is shown in IBUS_ENGINE_PREEDIT_COMMIT mode: when focus leaves, the client commits it
// itself, because ibus-daemon detaches the engine from a context that lost focus and a commit sent
// then is lost (measured, ibus 1.5.32). So focus out flushes the engine into a field that drops the
// commit (`discard`). Where the engine says what's staged differs from what shows (bada: a menu on
// the host's selection, staged ""), the preedit is shown in CLEAR mode instead.
class Field final : public bl::Field {
public:
    explicit Field(IBusEngine* engine, bool discard = false) : engine_(engine), discard_(discard) {}

    void Apply(const bl::Output& out) override {
        if (discard_) {
            ibus_engine_hide_lookup_table(engine_);
            return;
        }
        if (!out.commit.empty()) {
            if (out.replace_selection) DeleteSelection();
            ibus_engine_commit_text(engine_, ibus_text_new_from_string(out.commit.c_str()));
        }
        if (out.preedit.empty()) {
            ibus_engine_hide_preedit_text(engine_);
            ibus_engine_update_preedit_text_with_mode(engine_, ibus_text_new_from_static_string(""), 0, FALSE,
                                                      IBUS_ENGINE_PREEDIT_CLEAR);
        } else {
            IBusText* text = ibus_text_new_from_string(out.preedit.c_str());
            const guint n = Chars(out.preedit);
            // Underlined; double while the menu shows an entry other than entry 0.
            const bool moved = !out.menu.empty() && out.selected != out.zero;
            ibus_text_append_attribute(text, IBUS_ATTR_TYPE_UNDERLINE,
                                       moved ? IBUS_ATTR_UNDERLINE_DOUBLE : IBUS_ATTR_UNDERLINE_SINGLE, 0, n);
            ibus_engine_update_preedit_text_with_mode(engine_, text, n, TRUE,
                                                      StagedIs(out.preedit) ? IBUS_ENGINE_PREEDIT_COMMIT
                                                                            : IBUS_ENGINE_PREEDIT_CLEAR);
        }
        Menu(out);
    }

    std::string Selection() override {
        if (!(engine_->client_capabilities & IBUS_CAP_SURROUNDING_TEXT)) return {};
        IBusText* text = nullptr;
        guint cursor = 0, anchor = 0;
        ibus_engine_get_surrounding_text(engine_, &text, &cursor, &anchor);
        if (!text || cursor == anchor) return {};
        const gchar* s = ibus_text_get_text(text);
        const glong length = g_utf8_strlen(s, -1);
        const glong from = std::min(cursor, anchor), to = std::min<glong>(std::max(cursor, anchor), length);
        if (from >= to) return {};
        const gchar* a = g_utf8_offset_to_pointer(s, from);
        const gchar* b = g_utf8_offset_to_pointer(s, to);
        return std::string(a, static_cast<size_t>(b - a));
    }

    void Type(const std::string& text) override {
        ibus_engine_commit_text(engine_, ibus_text_new_from_string(text.c_str()));
    }

private:
    // Whether a flush would commit `preedit` (true when the engine doesn't say).
    bool StagedIs(const std::string& preedit) const;

    void DeleteSelection() {
        if (!(engine_->client_capabilities & IBUS_CAP_SURROUNDING_TEXT)) return;
        IBusText* text = nullptr;
        guint cursor = 0, anchor = 0;
        ibus_engine_get_surrounding_text(engine_, &text, &cursor, &anchor);
        if (!text || cursor == anchor) return;
        const guint from = std::min(cursor, anchor), to = std::max(cursor, anchor);
        ibus_engine_delete_surrounding_text(engine_, static_cast<gint>(from) - static_cast<gint>(cursor), to - from);
    }

    void Menu(const bl::Output& out) {
        if (out.menu.empty()) {
            ibus_engine_hide_lookup_table(engine_);
            return;
        }
        const guint n = static_cast<guint>(out.menu.size());
        const bool shown = out.selected < out.menu.size();
        IBusLookupTable* table = ibus_lookup_table_new(n, shown ? static_cast<guint>(out.selected) : 0, shown, FALSE);
        if (out.horizontal) ibus_lookup_table_set_orientation(table, IBUS_ORIENTATION_HORIZONTAL);
        for (guint row = 0; row < n; ++row) {
            std::string text = out.menu[row];
            if (row < out.notes.size() && !out.notes[row].empty()) text += "  " + out.notes[row];
            ibus_lookup_table_append_candidate(table, ibus_text_new_from_string(text.c_str()));
            // Rows above entry 0 are labelled with their entries (−2, −1); the rest have none.
            const std::string label = row < out.zero ? "\xE2\x88\x92" + std::to_string(out.zero - row) : "";
            ibus_lookup_table_append_label(table, ibus_text_new_from_string(label.c_str()));
        }
        ibus_engine_update_lookup_table(engine_, table, TRUE);
    }

    IBusEngine* engine_;
    bool discard_;
};

bool Field::StagedIs(const std::string& preedit) const {
    const auto state = bl::json::Parse(BADA_ENGINE(engine_)->context->EngineState());
    const bl::json::Value* staged = state ? state->Get("staged") : nullptr;
    return !staged || !staged->String() || *staged->String() == preedit;
}

bl::ContextInfo InfoOf(BadaEngine* e) {
    bl::ContextInfo info;
    info.secure = e->secure;  // IBus doesn't tell an engine the program's name: no app (docs/spec.md §8)
    return info;
}

void ShowMode(BadaEngine* e) {
    const std::string& label = g_session->Label();
    ibus_property_set_symbol(e->mode_prop, ibus_text_new_from_string(label.c_str()));
    const std::string name = g_session->manifest().display_name + (g_session->mode() == bl::Mode::kHenkan
                                                                      ? " (henkan)"
                                                                      : " (muhenkan)");
    ibus_property_set_label(e->mode_prop, ibus_text_new_from_string(name.c_str()));
    ibus_engine_update_property(IBUS_ENGINE(e), e->mode_prop);
}

gboolean ProcessKey(IBusEngine* engine, guint keyval, guint keycode, guint state) {
    BadaEngine* e = BADA_ENGINE(engine);
    bl::KeyDown key;
    key.code = keycode;  // IBus gives the evdev code (the X keycode − 8)
    key.sym = keyval;
    key.mods.shift = state & IBUS_SHIFT_MASK;
    key.mods.ctrl = state & IBUS_CONTROL_MASK;
    key.mods.alt = state & IBUS_MOD1_MASK;
    key.mods.meta = state & (IBUS_SUPER_MASK | IBUS_MOD4_MASK);
    key.caps = state & IBUS_LOCK_MASK;
    Field field(engine);
    const bl::Mode before = g_session->mode();
    gboolean eaten = FALSE;
    if (state & IBUS_RELEASE_MASK) {
        e->down->erase(keycode);
        e->context->KeyUp(key, field, InfoOf(e));
    } else {
        key.repeat = keycode && !e->down->insert(keycode).second;
        eaten = e->context->Key(key, field, InfoOf(e));
    }
    if (g_session->mode() != before) ShowMode(e);
    return eaten;
}

void FocusIn(IBusEngine* engine) {
    BadaEngine* e = BADA_ENGINE(engine);
    ibus_engine_register_properties(engine, e->props);
    // ibus-daemon sends a context's surrounding text only after the engine asked for it once.
    if (engine->client_capabilities & IBUS_CAP_SURROUNDING_TEXT) {
        IBusText* text = nullptr;
        guint cursor = 0, anchor = 0;
        ibus_engine_get_surrounding_text(engine, &text, &cursor, &anchor);
    }
    Field field(engine);
    e->context->FocusIn(field, InfoOf(e));
    ShowMode(e);
    IBUS_ENGINE_CLASS(bada_engine_parent_class)->focus_in(engine);
}

// A click in the text, the app's reset: commit what's staged (bada-compatible §7.6). On focus out
// the client commits the preedit itself (PREEDIT_COMMIT, above): the engine only lets go of it.
void Commit(IBusEngine* engine, bool by_client = false) {
    BadaEngine* e = BADA_ENGINE(engine);
    e->down->clear();
    Field field(engine, by_client);
    e->context->FocusOut(field);
}

void FocusOut(IBusEngine* engine) {
    Commit(engine, true);
    IBUS_ENGINE_CLASS(bada_engine_parent_class)->focus_out(engine);
}

void Reset(IBusEngine* engine) {
    Commit(engine);
    IBUS_ENGINE_CLASS(bada_engine_parent_class)->reset(engine);
}

void Disable(IBusEngine* engine) {
    Commit(engine);
    IBUS_ENGINE_CLASS(bada_engine_parent_class)->disable(engine);
}

void SetContentType(IBusEngine* engine, guint purpose, guint hints) {
    BadaEngine* e = BADA_ENGINE(engine);
    bool secure = purpose == IBUS_INPUT_PURPOSE_PASSWORD || purpose == IBUS_INPUT_PURPOSE_PIN;
#if IBUS_CHECK_VERSION(1, 5, 26)
    secure = secure || (hints & IBUS_INPUT_HINT_PRIVATE);
#else
    (void)hints;
#endif
    if (secure && !e->secure) Commit(engine);
    e->secure = secure;
}

// A click on the indicator switches mode.
void PropertyActivate(IBusEngine* engine, const gchar* name, guint) {
    BadaEngine* e = BADA_ENGINE(engine);
    if (std::strcmp(name, "InputMode") != 0) return;
    Field field(engine);
    const bl::Mode other = g_session->mode() == bl::Mode::kHenkan ? bl::Mode::kMuhenkan : bl::Mode::kHenkan;
    e->context->SwitchMode(other, field, InfoOf(e));
    ShowMode(e);
}

void bada_engine_init(BadaEngine* e) {
    e->context = new bl::Context(*g_session);
    e->down = new std::set<unsigned>();
    e->secure = FALSE;
    e->props = ibus_prop_list_new();
    g_object_ref_sink(e->props);
    // "InputMode": GNOME Shell shows this property's symbol as the indicator.
    e->mode_prop = ibus_property_new("InputMode", PROP_TYPE_NORMAL, ibus_text_new_from_static_string("mode"),
                                     nullptr, ibus_text_new_from_static_string("Switch mode"), TRUE, TRUE,
                                     PROP_STATE_UNCHECKED, nullptr);
    g_object_ref_sink(e->mode_prop);
    ibus_prop_list_append(e->props, e->mode_prop);
}

void Destroy(IBusObject* object) {
    BadaEngine* e = BADA_ENGINE(object);
    delete e->context;
    e->context = nullptr;
    delete e->down;
    e->down = nullptr;
    g_clear_object(&e->mode_prop);
    g_clear_object(&e->props);
    IBUS_OBJECT_CLASS(bada_engine_parent_class)->destroy(object);
}

void bada_engine_class_init(BadaEngineClass* klass) {
    IBUS_OBJECT_CLASS(klass)->destroy = Destroy;
    IBusEngineClass* e = IBUS_ENGINE_CLASS(klass);
    e->process_key_event = ProcessKey;
    e->focus_in = FocusIn;
    e->focus_out = FocusOut;
    e->reset = Reset;
    e->disable = Disable;
    e->set_content_type = SetContentType;
    e->property_activate = PropertyActivate;
}

// --- the component XML

std::string Escape(const std::string& s) {
    std::string out;
    for (char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default: out += c;
        }
    }
    return out;
}

std::string BusName(const std::string& id) {
    std::string name = "me.sjun.BadaLinux.";
    for (char c : id) name += (c == '-' || c == '.') ? '_' : c;
    return name;
}

std::string ComponentXml(const bl::Manifest& m, const std::string& exec) {
    const std::string manifest = m.file.string();
    std::string x = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<component>\n";
    x += "  <name>" + BusName(m.id) + "</name>\n";
    x += "  <description>" + Escape(m.display_name) + " (bada-linux)</description>\n";
    x += "  <exec>" + Escape(exec) + " --ibus --manifest " + Escape(manifest) + "</exec>\n";
    x += "  <version>" BADA_LINUX_VERSION "</version>\n";
    x += "  <author>team-mirae</author>\n  <license>CC0-1.0</license>\n";
    x += "  <homepage>https://github.com/team-mirae/bada-linux</homepage>\n  <textdomain></textdomain>\n";
    x += "  <engines>\n    <engine>\n";
    x += "      <name>" + Escape(m.id) + "</name>\n";
    x += "      <language>" + Escape(m.language) + "</language>\n";
    x += "      <license>CC0-1.0</license>\n      <author>team-mirae</author>\n";
    x += "      <icon>" + Escape(m.icon.empty() ? "input-keyboard" : m.icon) + "</icon>\n";
    x += "      <layout>" + Escape(m.layout) + "</layout>\n";
    x += "      <longname>" + Escape(m.display_name) + "</longname>\n";
    x += "      <description>" + Escape(m.description) + "</description>\n";
    x += "      <symbol>" + Escape(m.label_henkan) + "</symbol>\n";
    x += "      <rank>0</rank>\n    </engine>\n  </engines>\n</component>\n";
    return x;
}

}  // namespace

int main(int argc, char** argv) {
    std::string manifest_path, exec = BADA_IBUS_EXEC;
    bool by_daemon = false, xml = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--manifest" && i + 1 < argc) manifest_path = argv[++i];
        else if (a == "--exec" && i + 1 < argc) exec = argv[++i];
        else if (a == "--ibus") by_daemon = true;
        else if (a == "--xml") xml = true;
        else if (a == "--version") {
            std::printf("bada-ibus " BADA_LINUX_VERSION "\n");
            return 0;
        } else {
            std::fprintf(stderr, "usage: bada-ibus --manifest <ime.json> [--ibus] | --xml --manifest <ime.json> [--exec <path>]\n");
            return 2;
        }
    }
    if (manifest_path.empty()) {
        std::fprintf(stderr, "bada-ibus: --manifest is required\n");
        return 2;
    }
    std::string error;
    const std::filesystem::path absolute = std::filesystem::absolute(manifest_path);
    auto manifest = bl::LoadManifest(absolute, &error);
    if (!manifest) {
        std::fprintf(stderr, "bada-ibus: %s: %s\n", manifest_path.c_str(), error.c_str());
        return 1;
    }
    if (xml) {
        std::fputs(ComponentXml(*manifest, exec).c_str(), stdout);
        return 0;
    }

    bl::Publisher publisher(manifest->id);
    auto session = bl::Session::Load(*manifest, "ibus", BADA_LINUX_VERSION, &publisher, &error);
    if (!session) {
        std::fprintf(stderr, "bada-ibus: %s: %s\n", manifest->id.c_str(), error.c_str());
        return 1;
    }
    g_session = session.get();

    ibus_init();
    IBusBus* bus = ibus_bus_new();
    if (!ibus_bus_is_connected(bus)) {
        std::fprintf(stderr, "bada-ibus: can't connect to ibus-daemon\n");
        return 1;
    }
    g_signal_connect(bus, "disconnected", G_CALLBACK(+[](IBusBus*, gpointer) { ibus_quit(); }), nullptr);
    IBusFactory* factory = ibus_factory_new(ibus_bus_get_connection(bus));
    ibus_factory_add_engine(factory, manifest->id.c_str(), bada_engine_get_type());
    if (by_daemon) {
        ibus_bus_request_name(bus, BusName(manifest->id).c_str(), 0);
    } else {
        // Run by hand: register the component ourselves.
        IBusComponent* component = ibus_component_new(BusName(manifest->id).c_str(), manifest->display_name.c_str(),
                                                      BADA_LINUX_VERSION, "CC0-1.0", "team-mirae", "", "", "");
        ibus_component_add_engine(
            component, ibus_engine_desc_new(manifest->id.c_str(), manifest->display_name.c_str(),
                                            manifest->description.c_str(), manifest->language.c_str(), "CC0-1.0",
                                            "team-mirae", manifest->icon.empty() ? "input-keyboard" : manifest->icon.c_str(),
                                            manifest->layout.c_str()));
        ibus_bus_register_component(bus, component);
    }
    ibus_main();
    g_session = nullptr;
    return 0;
}
