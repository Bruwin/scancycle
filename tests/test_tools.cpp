#include "harness.hpp"

#include "assets/kit_generator.hpp"
#include "assets/material_library.hpp"
#include "ui/editor_model.hpp"

using namespace sc;

TEST(material_library_has_factory_defaults) {
    const MaterialLibrary library = MaterialLibrary::create_factory_defaults();

    CHECK(library.size() >= 9);
    // Ids are the index into the palette and a chunk stores ids, so a gap or a
    // repeated name would make one material unreachable from the renderer.
    for (std::size_t i = 0; i < library.materials().size(); ++i) {
        CHECK(library.materials()[i].id == static_cast<MaterialId>(i + 1));
        for (std::size_t j = i + 1; j < library.materials().size(); ++j) {
            CHECK(library.materials()[i].name != library.materials()[j].name);
        }
    }
    CHECK(library.find(kMaterialAir) == nullptr);

    const Material* floor = library.find(1);
    CHECK(floor != nullptr);
    if (floor == nullptr) {
        return;
    }
    CHECK(floor->name == "concrete_floor");
    CHECK(floor->roughness > 0.5f);
    CHECK(library.find(static_cast<MaterialId>(library.size() + 1)) == nullptr);
}

TEST(kit_generation_is_deterministic) {
    const Kit first = generate_machine_kit("press", 42);
    const Kit second = generate_machine_kit("press", 42);
    const Kit other = generate_machine_kit("press", 43);

    CHECK(first.name == "press");
    CHECK(first.pieces.size() == second.pieces.size());
    CHECK(first.pieces.size() >= 4);

    bool identical = true;
    for (std::size_t i = 0; i < first.pieces.size() && i < second.pieces.size(); ++i) {
        identical = identical && first.pieces[i].width == second.pieces[i].width &&
                    first.pieces[i].height == second.pieces[i].height &&
                    first.pieces[i].depth == second.pieces[i].depth &&
                    first.pieces[i].material == second.pieces[i].material;
    }
    CHECK(identical);

    // A different seed must actually change the geometry, otherwise the seed is
    // being ignored.
    bool differs = first.pieces.size() != other.pieces.size();
    for (std::size_t i = 0; i < first.pieces.size() && i < other.pieces.size(); ++i) {
        differs = differs || first.pieces[i].width != other.pieces[i].width ||
                  first.pieces[i].height != other.pieces[i].height ||
                  first.pieces[i].depth != other.pieces[i].depth;
    }
    CHECK(differs);
}

TEST(kit_pieces_stay_inside_a_machine_sized_box) {
    for (u64 seed = 1; seed <= 8; ++seed) {
        const Kit kit = generate_machine_kit("press", seed);
        bool inside = true;
        for (const KitPiece& piece : kit.pieces) {
            inside = inside && piece.x >= 0 && piece.y >= 0 && piece.z >= 0 &&
                     piece.x + static_cast<i32>(piece.width) <= 24 &&
                     piece.y + static_cast<i32>(piece.height) <= 24 &&
                     piece.z + static_cast<i32>(piece.depth) <= 24;
        }
        CHECK(inside);
    }
}

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
