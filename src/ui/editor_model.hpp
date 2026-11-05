#pragma once
// The Structured Text editor model: a text buffer with undo/redo and the
// diagnostics produced by the last compile. Deliberately headless — the real
// editor widget renders this, and every behaviour here is unit testable.
#include "core/types.hpp"
#include "plc/diagnostic.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace sc::ui {

class EditorModel {
public:
    void set_text(std::string text);
    const std::string& text() const { return text_; }

    std::size_t cursor() const { return cursor_; }
    void set_cursor(std::size_t position);

    void insert(std::string_view text);
    void erase_back();
    void move_line_up();
    void move_line_down();

    bool undo();
    bool redo();
    bool can_undo() const { return history_index_ > 0; }
    bool can_redo() const { return history_index_ + 1 < history_.size(); }

    void set_diagnostics(plc::Diagnostics diagnostics) { diagnostics_ = std::move(diagnostics); }
    const plc::Diagnostics& diagnostics() const { return diagnostics_; }

    bool dirty() const { return dirty_; }
    void mark_saved() { dirty_ = false; }

    std::size_t line_count() const;

private:
    void push_history();

    std::string text_;
    std::size_t cursor_ = 0;
    std::vector<std::string> history_{std::string()};
    std::size_t history_index_ = 0;
    plc::Diagnostics diagnostics_;
    bool dirty_ = false;
};

}  // namespace sc::ui
