#pragma once
// Places the procedural kit catalogue into the static voxel world. Kits become
// ordinary world voxels, so the existing chunk mesher, streaming and indirect
// draw path render them without a second scene graph.
#include "assets/kit_generator.hpp"
#include "core/types.hpp"
#include "world/world.hpp"

#include <array>

namespace sc {

struct KitPlacement {
    KitClass kind = KitClass::Press;
    i32 x = 0;
    i32 z = 0;
    u64 seed = 1;
};

// A compact production cell around the origin. Every kit stands on the floor
// plane and the footprints do not overlap. Coordinates are the kit's local
// origin, not its centre.
inline constexpr std::array<KitPlacement, 10> kFactoryKitLayout{{
    {KitClass::Press, -8, -8, 101},
    {KitClass::Conveyor, 8, -4, 102},
    {KitClass::ControlCabinet, -22, 2, 103},
    {KitClass::PhotoEye, -2, 6, 104},
    {KitClass::StackLight, -8, 6, 105},
    {KitClass::SafetyFence, -24, -12, 106},
    {KitClass::PalletRack, 2, -18, 107},
    {KitClass::PipeRun, -24, 12, 108},
    {KitClass::Barrel, 20, 4, 109},
    {KitClass::PalletLoad, 8, 4, 110},
}};

// Stamps one kit at a world offset. Returns how many voxel writes succeeded.
u32 place_kit(World& world, const Kit& kit, i32 origin_x, i32 origin_y, i32 origin_z);

// Stamps the default layout one voxel above a floor whose top layer is floor_y.
void place_factory_kits(World& world, i32 floor_y = 0);

}  // namespace sc
