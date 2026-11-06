#include "harness.hpp"

#include "ui/editor_model.hpp"

using namespace sc;

TEST(editor_insert_and_erase) {
    ui::EditorModel editor;
    editor.set_text("A");
    CHECK(editor.cursor() == 1);

    editor.insert("BC");
    CHECK(editor.text() == "ABC");

    editor.erase_back();
    CHECK(editor.text() == "AB");

    editor.set_cursor(0);
    editor.erase_back();
    CHECK(editor.text() == "AB");
}

TEST(editor_undo_and_redo) {
    ui::EditorModel editor;
    editor.set_text("A");
    CHECK(!editor.can_undo());

    editor.insert("BC");
    CHECK(editor.can_undo());

    CHECK(editor.undo());
    CHECK(editor.text() == "A");
    CHECK(editor.can_redo());

    CHECK(editor.redo());
    CHECK(editor.text() == "ABC");

    // A new edit after an undo discards the redo branch.
    CHECK(editor.undo());
    editor.insert("Z");
    CHECK(editor.text() == "AZ");
    CHECK(!editor.can_redo());
}

TEST(editor_tracks_lines_and_save_state) {
    ui::EditorModel editor;
    editor.set_text("PROGRAM P\nEND_PROGRAM");
    CHECK(editor.line_count() == 2);
    CHECK(editor.dirty());

    editor.mark_saved();
    CHECK(!editor.dirty());

    editor.insert("\n");
    CHECK(editor.dirty());
    CHECK(editor.line_count() == 3);
}
