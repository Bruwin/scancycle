#include "assets/kit_grammar.hpp"

namespace sc {
namespace {

constexpr MaterialId material_id(KitMaterial value) {
    return static_cast<MaterialId>(value);
}

void add_piece(Kit& kit, KitMaterial material, i32 x, i32 y, i32 z, u32 width, u32 height,
               u32 depth) {
    kit.pieces.push_back(
        KitPiece{material_id(material), x, y, z, width, height, depth});
}

void build_conveyor(Kit& kit, Pcg32& rng) {
    const u32 length = 16 + rng.bounded(5);
    const u32 width = 4 + rng.bounded(2);
    const u32 deck_y = 2 + rng.bounded(2);

    add_piece(kit, KitMaterial::Steel, 0, 0, 0, length, 1, width);
    add_piece(kit, KitMaterial::ConveyorGreen, 0, 1, 0, length, 1, 1);
    add_piece(kit, KitMaterial::ConveyorGreen, 0, 1, static_cast<i32>(width - 1),
              length, 1, 1);
    add_piece(kit, KitMaterial::Steel, 0, static_cast<i32>(deck_y - 1), 0, length, 1,
              width);
    add_piece(kit, KitMaterial::Rubber, 1, static_cast<i32>(deck_y), 1, length - 2, 1,
              width - 2);
    add_piece(kit, KitMaterial::Steel, 0, static_cast<i32>(deck_y), 1, 1, 1, width - 2);
    add_piece(kit, KitMaterial::Steel, static_cast<i32>(length - 1),
              static_cast<i32>(deck_y), 1, 1, 1, width - 2);
    add_piece(kit, KitMaterial::ConveyorGreen, 1, static_cast<i32>(deck_y + 1), 0,
              length - 2, 1, 1);
    add_piece(kit, KitMaterial::ConveyorGreen, 1, static_cast<i32>(deck_y + 1),
              static_cast<i32>(width - 1), length - 2, 1, 1);

    const u32 legs = 3 + rng.bounded(2);
    for (u32 i = 0; i < legs; ++i) {
        const u32 x = 1 + (length - 3) * i / (legs - 1);
        add_piece(kit, KitMaterial::Steel, static_cast<i32>(x), 0, 1, 1,
                  static_cast<u32>(deck_y - 1), 1);
        add_piece(kit, KitMaterial::Steel, static_cast<i32>(x), 0,
                  static_cast<i32>(width - 2), 1, static_cast<u32>(deck_y - 1), 1);
    }

    // A small photo-eye on the outfeed edge makes the conveyor read as part of a
    // sensing line, not just a green table.
    if (rng.bounded(2) == 0) {
        add_piece(kit, KitMaterial::SafetyYellow, static_cast<i32>(length / 2), 1,
                  static_cast<i32>(width), 2, 3, 1);
        add_piece(kit, KitMaterial::EStopRed, static_cast<i32>(length / 2), 4,
                  static_cast<i32>(width), 1, 1, 1);
    }
}

void build_press(Kit& kit, Pcg32& rng) {
    const u32 width = 11 + rng.bounded(3);
    const u32 depth = 7 + rng.bounded(2);
    const u32 height = 10 + rng.bounded(3);

    add_piece(kit, KitMaterial::Steel, 0, 0, 0, width, 1, depth);
    add_piece(kit, KitMaterial::SafetyYellow, 0, 1, 0, width, 1, depth);
    add_piece(kit, KitMaterial::Steel, 1, 1, 1, 2, height - 1, 2);
    add_piece(kit, KitMaterial::Steel, static_cast<i32>(width - 3), 1, 1, 2,
              height - 1, 2);
    add_piece(kit, KitMaterial::MachineBlue, 0, static_cast<i32>(height - 1), 0, width,
              3, depth);
    add_piece(kit, KitMaterial::Steel, 3, 3, 2, width - 6, 1, depth - 4);
    add_piece(kit, KitMaterial::MachineBlue, static_cast<i32>(width / 2 - 1), 3, 3, 2,
              height - 4, depth - 4);
    add_piece(kit, KitMaterial::SafetyYellow, static_cast<i32>(width / 2 - 2),
              static_cast<i32>(height - 4), 2, 4, 1, depth - 4);
    add_piece(kit, KitMaterial::SafetyYellow, 1, 2, static_cast<i32>(depth - 1), 1,
              height - 3, 1);
    add_piece(kit, KitMaterial::EStopRed, 2, static_cast<i32>(height - 4),
              static_cast<i32>(depth - 1), 1, 1, 1);
    add_piece(kit, KitMaterial::PaintedWhite, static_cast<i32>(width - 4), 2,
              static_cast<i32>(depth - 1), 1, height - 4, 1);
}

void build_control_cabinet(Kit& kit, Pcg32& rng) {
    const u32 width = 5 + rng.bounded(2);
    const u32 depth = 3 + rng.bounded(2);
    const u32 height = 10 + rng.bounded(4);

    add_piece(kit, KitMaterial::Steel, 0, 0, 0, width, 1, depth);
    add_piece(kit, KitMaterial::MachineBlue, 0, 1, 0, width, height - 1, depth);
    add_piece(kit, KitMaterial::PaintedWhite, 1, 2, static_cast<i32>(depth - 1),
              width - 2, height - 4, 1);
    add_piece(kit, KitMaterial::Steel, 2, 3, static_cast<i32>(depth - 1), 1, 1, 1);
    add_piece(kit, KitMaterial::ConveyorGreen, 1, static_cast<i32>(height - 3),
              static_cast<i32>(depth - 1), 1, 1, 1);
    add_piece(kit, KitMaterial::EStopRed, 2, static_cast<i32>(height - 3),
              static_cast<i32>(depth - 1), 1, 1, 1);
    add_piece(kit, KitMaterial::Dark, 1, 2, static_cast<i32>(depth - 1), width - 2, 1,
              1);
    add_piece(kit, KitMaterial::Steel, 0, static_cast<i32>(height), 0, width, 1,
              depth);
}

void build_photo_eye(Kit& kit, Pcg32& rng) {
    const u32 post_height = 8 + rng.bounded(3);
    const u32 foot = 2 + rng.bounded(2);

    add_piece(kit, KitMaterial::Steel, 0, 0, 0, foot, 1, foot);
    add_piece(kit, KitMaterial::Rubber, 1, 1, 1, 1, post_height - 1, 1);
    add_piece(kit, KitMaterial::SafetyYellow, 0, static_cast<i32>(post_height), 0, 3,
              2, 3);
    add_piece(kit, KitMaterial::Dark, 1, static_cast<i32>(post_height), 0, 1, 1, 1);
    add_piece(kit, KitMaterial::EStopRed, 1, static_cast<i32>(post_height + 1), 0, 1,
              1, 1);
}

void build_stack_light(Kit& kit, Pcg32& rng) {
    const u32 base = 3 + rng.bounded(2);
    const u32 pole = 2 + rng.bounded(3);

    add_piece(kit, KitMaterial::Steel, 0, 0, 0, base, 1, base);
    add_piece(kit, KitMaterial::Rubber, static_cast<i32>(base / 2), 1,
              static_cast<i32>(base / 2), 1, pole, 1);
    i32 y = static_cast<i32>(1 + pole);

    add_piece(kit, KitMaterial::Dark, 0, y, 0, base, 1, base);
    ++y;
    add_piece(kit, KitMaterial::ConveyorGreen, 0, y, 0, base, 1, base);
    ++y;
    add_piece(kit, KitMaterial::Dark, 0, y, 0, base, 1, base);
    ++y;
    add_piece(kit, KitMaterial::SafetyYellow, 0, y, 0, base, 1, base);
    ++y;
    add_piece(kit, KitMaterial::Dark, 0, y, 0, base, 1, base);
    ++y;
    add_piece(kit, KitMaterial::EStopRed, 0, y, 0, base, 1, base);
    ++y;
    add_piece(kit, KitMaterial::Steel, 0, y, 0, base, 1, base);
}

void build_safety_fence(Kit& kit, Pcg32& rng) {
    const u32 length = 12 + rng.bounded(5);
    const u32 height = 7 + rng.bounded(2);

    add_piece(kit, KitMaterial::Steel, 0, 0, 0, 2, 1, 2);
    add_piece(kit, KitMaterial::Steel, static_cast<i32>(length - 2), 0, 0, 2, 1, 2);
    add_piece(kit, KitMaterial::SafetyYellow, 0, 1, 0, 1, height, 1);
    add_piece(kit, KitMaterial::SafetyYellow, static_cast<i32>(length - 1), 1, 0, 1,
              height, 1);
    add_piece(kit, KitMaterial::Steel, 0, static_cast<i32>(height), 0, length, 1, 1);
    add_piece(kit, KitMaterial::Steel, 0, 1, 0, length, 1, 1);
    add_piece(kit, KitMaterial::Dark, 1, 2, 0, length - 2, height - 2, 1);
    if (rng.bounded(2) == 0) {
        add_piece(kit, KitMaterial::SafetyYellow, static_cast<i32>(length / 2), 1, 0,
                  1, height, 1);
    }
}

void build_pallet_rack(Kit& kit, Pcg32& rng) {
    const u32 length = 12 + rng.bounded(5);
    const u32 depth = 5 + rng.bounded(2);
    const u32 height = 12 + rng.bounded(4);

    for (int corner = 0; corner < 4; ++corner) {
        const i32 x = (corner & 1) != 0 ? static_cast<i32>(length - 1) : 0;
        const i32 z = (corner & 2) != 0 ? static_cast<i32>(depth - 1) : 0;
        add_piece(kit, KitMaterial::SafetyYellow, x, 0, z, 1, height, 1);
    }

    const u32 levels = 2 + rng.bounded(2);
    for (u32 level = 1; level <= levels; ++level) {
        const i32 y = static_cast<i32>(level * (height - 2) / (levels + 1));
        add_piece(kit, KitMaterial::Steel, 0, y, 0, length, 1, 1);
        add_piece(kit, KitMaterial::Steel, 0, y, static_cast<i32>(depth - 1), length,
                  1, 1);
        add_piece(kit, KitMaterial::Timber, 1, y + 1, 1, length - 2, 1, depth - 2);
        if (rng.bounded(2) == 0) {
            add_piece(kit, KitMaterial::Timber, 2, y + 2, 1, 3, 2, depth - 2);
            add_piece(kit, KitMaterial::SafetyYellow, 2, y + 2,
                      static_cast<i32>(depth - 2), 3, 1, 1);
        }
    }
}

void build_pipe_run(Kit& kit, Pcg32& rng) {
    const u32 length = 14 + rng.bounded(5);
    const u32 pipe_y = 6 + rng.bounded(3);

    add_piece(kit, KitMaterial::Steel, 1, 0, 0, 1, pipe_y, 2);
    add_piece(kit, KitMaterial::Steel, static_cast<i32>(length - 3), 0, 0, 1, pipe_y,
              2);
    add_piece(kit, KitMaterial::Steel, 0, static_cast<i32>(pipe_y), 0, length, 2, 2);
    add_piece(kit, KitMaterial::EStopRed, static_cast<i32>(length / 2),
              static_cast<i32>(pipe_y + 2), 0, 2, 1, 2);
    add_piece(kit, KitMaterial::SafetyYellow, static_cast<i32>(length / 2 - 1),
              static_cast<i32>(pipe_y + 3), 0, 4, 1, 1);
    add_piece(kit, KitMaterial::SafetyYellow, static_cast<i32>(length / 2),
              static_cast<i32>(pipe_y + 3), 0, 1, 1, 2);
    add_piece(kit, KitMaterial::Dark, 2, static_cast<i32>(pipe_y - 1), 0, 1, 4, 3);
    add_piece(kit, KitMaterial::Dark, static_cast<i32>(length - 4),
              static_cast<i32>(pipe_y - 1), 0, 1, 4, 3);
    add_piece(kit, KitMaterial::Steel, static_cast<i32>(length),
              static_cast<i32>(pipe_y - 2), 0, 2, 2, 2);
    add_piece(kit, KitMaterial::Steel, static_cast<i32>(length), 1, 0, 2,
              pipe_y - 2, 2);
    add_piece(kit, KitMaterial::Dark, static_cast<i32>(length), 0, 0, 2, 2, 2);
}

void build_barrel(Kit& kit, Pcg32& rng) {
    const u32 size = 4 + rng.bounded(2);
    const u32 height = 5 + rng.bounded(2);

    add_piece(kit, KitMaterial::Steel, 0, 0, 0, size, 1, size);
    add_piece(kit, KitMaterial::MachineBlue, 1, 1, 1, size - 2, height - 2, size - 2);
    add_piece(kit, KitMaterial::Rubber, 0, 2, 0, size, 1, size);
    add_piece(kit, KitMaterial::Rubber, 0, static_cast<i32>(height - 2), 0, size, 1,
              size);
    add_piece(kit, KitMaterial::Steel, 0, static_cast<i32>(height - 1), 0, size, 1,
              size);
    add_piece(kit, KitMaterial::SafetyYellow, static_cast<i32>(size - 1), 2, 1, 1, 1,
              1);
}

void build_pallet_load(Kit& kit, Pcg32& rng) {
    const u32 length = 8 + rng.bounded(3);
    const u32 depth = 6 + rng.bounded(3);
    const u32 crate_height = 4 + rng.bounded(3);

    add_piece(kit, KitMaterial::Timber, 0, 0, 0, length, 1, depth);
    add_piece(kit, KitMaterial::Timber, 0, 1, 1, length, 1, 1);
    add_piece(kit, KitMaterial::Timber, 0, 1, static_cast<i32>(depth - 2), length, 1,
              1);
    const u32 slats = 3 + rng.bounded(2);
    for (u32 i = 0; i < slats; ++i) {
        const u32 z = 1 + (depth - 2) * i / (slats - 1);
        add_piece(kit, KitMaterial::Timber, 0, 2, static_cast<i32>(z), length, 1, 1);
    }
    add_piece(kit, KitMaterial::Timber, 1, 3, 1, length - 2, crate_height, depth - 2);
    add_piece(kit, KitMaterial::Steel, 1, 3, 0, length - 2, 1, 1);
    add_piece(kit, KitMaterial::Steel, 1, 3, static_cast<i32>(depth - 1), length - 2,
              1, 1);
    add_piece(kit, KitMaterial::PaintedWhite, 2, 4, static_cast<i32>(depth - 1), 2, 2,
              1);
}

}  // namespace

namespace detail {

void add_kit_pieces(Kit& kit, Pcg32& rng) {
    switch (kit.kind) {
        case KitClass::Conveyor:
            build_conveyor(kit, rng);
            break;
        case KitClass::Press:
            build_press(kit, rng);
            break;
        case KitClass::ControlCabinet:
            build_control_cabinet(kit, rng);
            break;
        case KitClass::PhotoEye:
            build_photo_eye(kit, rng);
            break;
        case KitClass::StackLight:
            build_stack_light(kit, rng);
            break;
        case KitClass::SafetyFence:
            build_safety_fence(kit, rng);
            break;
        case KitClass::PalletRack:
            build_pallet_rack(kit, rng);
            break;
        case KitClass::PipeRun:
            build_pipe_run(kit, rng);
            break;
        case KitClass::Barrel:
            build_barrel(kit, rng);
            break;
        case KitClass::PalletLoad:
            build_pallet_load(kit, rng);
            break;
    }
}

}  // namespace detail
}  // namespace sc
