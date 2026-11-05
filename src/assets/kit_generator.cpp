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

    return kit;
}

}  // namespace sc
