// The Output object of bada-compatible v1 (bada-compatible docs/bada-compatible-v1.md §6): what
// the engine asks the host to do after an event. Every field is optional to read; a missing field is
// its default, and invalid JSON is an Output with nothing in it (nothing consumed).
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace bl {

struct Output {
    std::string commit;            // text to insert now (UTF-8)
    bool replace_selection = false;  // commit replaces the selection even when text is marked
    std::string preedit;           // the marked text after the commit; "": nothing marked
    std::vector<std::string> menu;  // the candidate menu's rows, top to bottom
    size_t selected = 0;           // the highlighted row
    size_t zero = 0;               // the row of entry 0; rows above it are negative entries
    bool consumed = false;         // false: the app gets the key too, after the commit
    bool horizontal = false;       // the menu is one row of single characters
    std::vector<std::string> notes;  // per row: a note beside it
    std::vector<bool> japanese;    // per row: draw in a Japanese font
    std::optional<std::string> need_selection;  // the engine wants the host's selected text
};

Output ParseOutput(std::string_view json);

}  // namespace bl
