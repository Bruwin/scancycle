#include "harness.hpp"

#include "assets/kit_generator.hpp"
#include "assets/material_library.hpp"

using namespace sc;

namespace {

bool has_material(const Kit& kit, KitMaterial material) {
    const MaterialId wanted = static_cast<MaterialId>(material);
    for (const KitPiece& piece : kit.pieces) {
        if (piece.material == wanted) {
            return true;
        }
    }
    return false;
}

i32 extent_x(const Kit& kit) {
    i32 result = 0;
    for (const KitPiece& piece : kit.pieces) {
        const i32 edge = piece.x + static_cast<i32>(piece.width);
        if (edge > result) {
            result = edge;
        }
    }
    return result;
}

i32 extent_y(const Kit& kit) {
    i32 result = 0;
    for (const KitPiece& piece : kit.pieces) {
        const i32 edge = piece.y + static_cast<i32>(piece.height);
        if (edge > result) {
            result = edge;
        }
    }
    return result;
}

i32 extent_z(const Kit& kit) {
    i32 result = 0;
    for (const KitPiece& piece : kit.pieces) {
        const i32 edge = piece.z + static_cast<i32>(piece.depth);
        if (edge > result) {
            result = edge;
        }
    }
    return result;
}

i32 highest_material_y(const Kit& kit, KitMaterial material) {
    const MaterialId wanted = static_cast<MaterialId>(material);
    i32 result = -1;
    for (const KitPiece& piece : kit.pieces) {
        if (piece.material != wanted) {
            continue;
        }
        const i32 edge = piece.y + static_cast<i32>(piece.height);
        if (edge > result) {
            result = edge;
        }
    }
    return result;
}

bool same_pieces(const Kit& first, const Kit& second) {
    if (first.pieces.size() != second.pieces.size()) {
        return false;
    }
    for (std::size_t i = 0; i < first.pieces.size(); ++i) {
        const KitPiece& a = first.pieces[i];
        const KitPiece& b = second.pieces[i];
        if (a.material != b.material || a.x != b.x || a.y != b.y || a.z != b.z ||
            a.width != b.width || a.height != b.height || a.depth != b.depth) {
            return false;
        }
    }
    return true;
}

}  // namespace

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

TEST(kit_catalog_is_named_and_typed) {
    CHECK(kKitCatalog.size() == 10);
    for (std::size_t i = 0; i < kKitCatalog.size(); ++i) {
        const KitClass kind = kKitCatalog[i];
        const std::string_view name = kit_class_name(kind);
        CHECK(!name.empty());
        for (std::size_t j = i + 1; j < kKitCatalog.size(); ++j) {
            CHECK(name != kit_class_name(kKitCatalog[j]));
        }

        const Kit kit = generate_machine_kit(kind, 11);
        CHECK(kit.kind == kind);
        CHECK(kit.name == std::string(name));
        CHECK(kit.pieces.size() >= 5);

        const std::optional<KitClass> found = kit_class_from_name(name);
        CHECK(found.has_value());
        if (found.has_value()) {
            CHECK(*found == kind);
        }
    }
    CHECK(!kit_class_from_name("not_a_kit").has_value());
}

TEST(kit_generation_is_deterministic) {
    const Kit first = generate_machine_kit(KitClass::Press, 42);
    const Kit second = generate_machine_kit(KitClass::Press, 42);
    const Kit other = generate_machine_kit(KitClass::Press, 43);

    CHECK(first.name == "press");
    CHECK(first.pieces.size() == second.pieces.size());
    CHECK(first.pieces.size() >= 4);
    CHECK(same_pieces(first, second));
    CHECK(!same_pieces(first, other));
}

TEST(every_kit_class_reacts_to_its_seed) {
    for (const KitClass kind : kKitCatalog) {
        const Kit first = generate_machine_kit(kind, 1);
        bool differs = false;
        for (u64 seed = 2; seed <= 8; ++seed) {
            if (!same_pieces(first, generate_machine_kit(kind, seed))) {
                differs = true;
                break;
            }
        }
        CHECK(differs);
    }
}

TEST(kit_pieces_stay_inside_a_machine_sized_box) {
    for (const KitClass kind : kKitCatalog) {
        for (u64 seed = 1; seed <= 8; ++seed) {
            const Kit kit = generate_machine_kit(kind, seed);
            bool inside = true;
            for (const KitPiece& piece : kit.pieces) {
                inside = inside && piece.x >= 0 && piece.y >= 0 && piece.z >= 0 &&
                         piece.width > 0 && piece.height > 0 && piece.depth > 0 &&
                         piece.x + static_cast<i32>(piece.width) <= 24 &&
                         piece.y + static_cast<i32>(piece.height) <= 24 &&
                         piece.z + static_cast<i32>(piece.depth) <= 24;
            }
            CHECK(inside);
        }
    }
}

TEST(kit_classes_have_recognizable_industrial_signatures) {
    const Kit conveyor = generate_machine_kit(KitClass::Conveyor, 7);
    CHECK(extent_x(conveyor) >= 16);
    CHECK(extent_y(conveyor) <= 6);
    CHECK(has_material(conveyor, KitMaterial::ConveyorGreen));
    CHECK(has_material(conveyor, KitMaterial::Rubber));

    const Kit press = generate_machine_kit(KitClass::Press, 7);
    CHECK(extent_y(press) >= 12);
    CHECK(has_material(press, KitMaterial::MachineBlue));
    CHECK(has_material(press, KitMaterial::SafetyYellow));
    CHECK(has_material(press, KitMaterial::EStopRed));

    const Kit cabinet = generate_machine_kit(KitClass::ControlCabinet, 7);
    CHECK(extent_y(cabinet) >= 11);
    CHECK(extent_x(cabinet) <= 6);
    CHECK(extent_z(cabinet) <= 4);
    CHECK(has_material(cabinet, KitMaterial::PaintedWhite));
    CHECK(has_material(cabinet, KitMaterial::ConveyorGreen));
    CHECK(has_material(cabinet, KitMaterial::EStopRed));

    const Kit eye = generate_machine_kit(KitClass::PhotoEye, 7);
    CHECK(extent_y(eye) >= 10);
    CHECK(extent_x(eye) <= 3);
    CHECK(extent_z(eye) <= 3);
    CHECK(has_material(eye, KitMaterial::SafetyYellow));
    CHECK(has_material(eye, KitMaterial::EStopRed));

    const Kit stack = generate_machine_kit(KitClass::StackLight, 7);
    CHECK(extent_y(stack) >= 10);
    CHECK(extent_x(stack) <= 4);
    CHECK(has_material(stack, KitMaterial::ConveyorGreen));
    CHECK(has_material(stack, KitMaterial::SafetyYellow));
    CHECK(has_material(stack, KitMaterial::EStopRed));
    CHECK(highest_material_y(stack, KitMaterial::EStopRed) >
          highest_material_y(stack, KitMaterial::ConveyorGreen));

    const Kit fence = generate_machine_kit(KitClass::SafetyFence, 7);
    CHECK(extent_x(fence) >= 12);
    CHECK(extent_y(fence) >= 8);
    CHECK(has_material(fence, KitMaterial::SafetyYellow));
    CHECK(has_material(fence, KitMaterial::Dark));

    const Kit rack = generate_machine_kit(KitClass::PalletRack, 7);
    CHECK(extent_x(rack) >= 12);
    CHECK(extent_y(rack) >= 12);
    CHECK(has_material(rack, KitMaterial::SafetyYellow));
    CHECK(has_material(rack, KitMaterial::Timber));

    const Kit pipe = generate_machine_kit(KitClass::PipeRun, 7);
    CHECK(extent_x(pipe) >= 16);
    CHECK(extent_y(pipe) >= 10);
    CHECK(has_material(pipe, KitMaterial::SafetyYellow));
    CHECK(has_material(pipe, KitMaterial::EStopRed));
    CHECK(has_material(pipe, KitMaterial::Dark));

    const Kit barrel = generate_machine_kit(KitClass::Barrel, 7);
    CHECK(extent_x(barrel) <= 5);
    CHECK(extent_z(barrel) <= 5);
    CHECK(extent_y(barrel) <= 6);
    CHECK(has_material(barrel, KitMaterial::MachineBlue));
    CHECK(has_material(barrel, KitMaterial::Rubber));
    CHECK(has_material(barrel, KitMaterial::SafetyYellow));

    const Kit pallet = generate_machine_kit(KitClass::PalletLoad, 7);
    CHECK(extent_x(pallet) >= 8);
    CHECK(extent_y(pallet) >= 7);
    CHECK(has_material(pallet, KitMaterial::Timber));
    CHECK(has_material(pallet, KitMaterial::Steel));
}
