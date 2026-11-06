#include "assets/kit_scene.hpp"

namespace sc {

u32 place_kit(World& world, const Kit& kit, i32 origin_x, i32 origin_y, i32 origin_z) {
    u32 written = 0;
    for (const KitPiece& piece : kit.pieces) {
        for (u32 dy = 0; dy < piece.height; ++dy) {
            for (u32 dz = 0; dz < piece.depth; ++dz) {
                for (u32 dx = 0; dx < piece.width; ++dx) {
                    const i32 x = origin_x + piece.x + static_cast<i32>(dx);
                    const i32 y = origin_y + piece.y + static_cast<i32>(dy);
                    const i32 z = origin_z + piece.z + static_cast<i32>(dz);
                    if (world.set_voxel(x, y, z, piece.material)) {
                        ++written;
                    }
                }
            }
        }
    }
    return written;
}

void place_factory_kits(World& world, i32 floor_y) {
    const i32 origin_y = floor_y + 1;
    for (const KitPlacement& placement : kFactoryKitLayout) {
        const Kit kit = generate_machine_kit(placement.kind, placement.seed);
        (void)place_kit(world, kit, placement.x, origin_y, placement.z);
    }
}

}  // namespace sc
