#include "output.h"

#include "json.h"

namespace bl {

Output ParseOutput(std::string_view text) {
    Output out;
    const auto doc = json::Parse(text);
    if (!doc || !doc->IsObject()) return out;
    const json::Value& v = *doc;
    out.commit = v.GetString("commit");
    out.replace_selection = v.GetBool("replace_selection");
    out.preedit = v.GetString("preedit");
    out.consumed = v.GetBool("consumed");
    out.horizontal = v.GetBool("horizontal");
    const auto index = [&](const char* key) {
        const double d = v.GetNumber(key, 0);
        return d > 0 && d < 1e9 ? static_cast<size_t>(d) : size_t{0};
    };
    out.selected = index("selected");
    out.zero = index("zero");
    if (const json::Value* menu = v.Get("menu"); menu && menu->IsArray()) {
        for (const json::Value& row : menu->array) out.menu.push_back(row.String() ? *row.String() : "");
    }
    if (const json::Value* notes = v.Get("notes"); notes && notes->IsArray()) {
        for (const json::Value& row : notes->array) out.notes.push_back(row.String() ? *row.String() : "");
    }
    if (const json::Value* japanese = v.Get("japanese"); japanese && japanese->IsArray()) {
        for (const json::Value& row : japanese->array) {
            out.japanese.push_back(row.type == json::Value::Type::kBool && row.boolean);
        }
    }
    if (const json::Value* need = v.Get("need_selection"); need && need->String()) {
        out.need_selection = *need->String();
    }
    return out;
}

}  // namespace bl
