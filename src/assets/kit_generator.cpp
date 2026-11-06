#include "assets/kit_generator.hpp"

#include "core/deterministic_rng.hpp"

namespace sc {

Kit generate_machine_kit(const std::string& name, u64 seed) {
    Pcg32 rng(seed, 0x6b17ULL);

    Kit kit;
    kit.name = name;

    const u32 body_width = 4 + rng.bounded(4);
    const u32 body_height = 3 + rng.bounded(5);
    const u32 body_depth = 4 + rng.bounded(4);

    // Base plinth.
    kit.pieces.push_back(KitPiece{1, 0, 0, 0, body_width + 2, 1, body_depth + 2});
    // Main body.
    kit.pieces.push_back(KitPiece{2, 1, 1, 1, body_width, body_height, body_depth});
    // Access panel on the front face.
    kit.pieces.push_back(KitPiece{3, 1 + static_cast<i32>(body_width / 3), 1,
                                  1 + static_cast<i32>(body_depth), body_width / 2, 2, 1});
    // Hazard stripe along the base.
    kit.pieces.push_back(KitPiece{3, 0, 1, 0, body_width + 2, 1, 1});

    if (rng.bounded(2) == 0) {
        // Stack light on top.
        const u32 height = 2 + rng.bounded(3);
        kit.pieces.push_back(KitPiece{6, static_cast<i32>(body_width / 2) + 1,
                                      static_cast<i32>(body_height) + 1,
                                      static_cast<i32>(body_depth / 2) + 1, 1, height, 1});
    }

    // A photo-eye post at the front edge. Most machines get one: it is the part a
    // student has to find before they can wire the cell.
    if (rng.bounded(4) != 0) {
        const u32 post_height = 2 + rng.bounded(3);
        kit.pieces.push_back(KitPiece{3, static_cast<i32>(body_width) + 1, 1,
                                      static_cast<i32>(body_depth) / 2, 1, post_height, 1});
    }

    // A guard rail down one side, so not every kit is an open box.
    if (rng.bounded(2) == 0) {
        kit.pieces.push_back(KitPiece{5, 0, 2, 0, 1, 2, body_depth + 2});
    }

    // A conveyor stub leaving the body, which is what makes a kit read as part of
    // a line instead of a lone cabinet.
    if (rng.bounded(3) == 0) {
        kit.pieces.push_back(KitPiece{2, static_cast<i32>(body_width) + 2, 1,
                                       static_cast<i32>(body_depth) / 3, 4, 1,
                                      body_depth / 2});
    }

    return kit;
}

}  // namespace sc
