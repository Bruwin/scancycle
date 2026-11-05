#include "ui/editor_model.hpp"

#include <algorithm>
#include <utility>

namespace sc::ui {
namespace {

constexpr std::size_t kMaxHistory = 100;

}  // namespace

void EditorModel::set_text(std::string text) {
    text_ = std::move(text);
    cursor_ = text_.size();
    history_.assign(1, text_);
    history_index_ = 0;
    dirty_ = true;
}

void EditorModel::set_cursor(std::size_t position) {
    cursor_ = std::min(position, text_.size());
}

void EditorModel::push_history() {
    history_.resize(history_index_ + 1);
    history_.push_back(text_);
    if (history_.size() > kMaxHistory) {
        history_.erase(history_.begin());
    }
    history_index_ = history_.size() - 1;
    dirty_ = true;
}

void EditorModel::insert(std::string_view text) {
    text_.insert(cursor_, text);
    cursor_ += text.size();
    push_history();
}

void EditorModel::erase_back() {
    if (cursor_ == 0) {
        return;
    }
    text_.erase(cursor_ - 1, 1);
    --cursor_;
    push_history();
}

void EditorModel::move_line_up() {
    const std::size_t line_start = text_.rfind('\n', cursor_ == 0 ? 0 : cursor_ - 1);
    if (line_start == std::string::npos) {
        return;
    }
    const std::size_t previous_start = line_start == 0
                                           ? std::string::npos
                                           : text_.rfind('\n', line_start - 1);
    if (previous_start == std::string::npos && line_start == 0) {
        return;
    }
    const std::size_t target = previous_start == std::string::npos ? 0 : previous_start + 1;
    set_cursor(target);
}

void EditorModel::move_line_down() {
    const std::size_t line_end = text_.find('\n', cursor_);
    if (line_end == std::string::npos) {
        return;
    }
    set_cursor(line_end + 1);
}

bool EditorModel::undo() {
    if (!can_undo()) {
        return false;
    }
    --history_index_;
    text_ = history_[history_index_];
    cursor_ = std::min(cursor_, text_.size());
    dirty_ = true;
    return true;
}

bool EditorModel::redo() {
    if (!can_redo()) {
        return false;
    }
    ++history_index_;
    text_ = history_[history_index_];
    cursor_ = std::min(cursor_, text_.size());
    dirty_ = true;
    return true;
}

std::size_t EditorModel::line_count() const {
    if (text_.empty()) {
        return 1;
    }
    std::size_t lines = 1;
    for (char character : text_) {
        if (character == '\n') {
            ++lines;
        }
    }
    return lines;
}

}  // namespace sc::ui
