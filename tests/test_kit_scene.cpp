#include "harness.hpp"

#include "assets/kit_generator.hpp"
#include "assets/kit_scene.hpp"
#include "world/world.hpp"

using namespace sc;

namespace {

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

bool contains(const KitPiece& piece, i32 x, i32 y, i32 z) {
    return x >= piece.x && x < piece.x + static_cast<i32>(piece.width) &&
           y >= piece.y && y < piece.y + static_cast<i32>(piece.height) &&
           z >= piece.z && z < piece.z + static_cast<i32>(piece.depth);
}

MaterialId expected_voxel(const Kit& kit, i32 x, i32 y, i32 z) {
    MaterialId material = kMaterialAir;
    for (const KitPiece& piece : kit.pieces) {
        if (contains(piece, x, y, z)) {
            material = piece.material;
        }
    }
    return material;
}

struct Footprint {
    i32 min_x = 0;
    i32 max_x = 0;
    i32 min_z = 0;
    i32 max_z = 0;
};

Footprint footprint_of(const KitPlacement& placement, const Kit& kit) {
    return Footprint{placement.x, placement.x + extent_x(kit), placement.z,
                     placement.z + extent_z(kit)};
}

bool overlaps(const Footprint& a, const Footprint& b) {
    return a.min_x < b.max_x && b.min_x < a.max_x && a.min_z < b.max_z &&
           b.min_z < a.max_z;
}

u32 volume_of(const Kit& kit) {
    u32 volume = 0;
    for (const KitPiece& piece : kit.pieces) {
        volume += piece.width * piece.height * piece.depth;
    }
    return volume;
}

}  // namespace

TEST(placing_one_kit_writes_every_piece_at_the_offset) {
    const Kit kit = generate_machine_kit(KitClass::Barrel, 7);
    World world;

    CHECK(place_kit(world, kit, 4, 6, 8) == volume_of(kit));
    for (const KitPiece& piece : kit.pieces) {
        CHECK(world.voxel_at(4 + piece.x, 6 + piece.y, 8 + piece.z) == piece.material);
    }
    CHECK(world.voxel_at(3, 6, 8) == kMaterialAir);
    CHECK(world.voxel_at(4, 5, 8) == kMaterialAir);
}

TEST(factory_layout_has_ten_distinct_non_overlapping_footprints) {
    CHECK(kFactoryKitLayout.size() == 10);
    for (std::size_t i = 0; i < kFactoryKitLayout.size(); ++i) {
        const KitPlacement& a = kFactoryKitLayout[i];
        const Kit first = generate_machine_kit(a.kind, a.seed);
        CHECK(extent_x(first) <= 24);
        CHECK(extent_y(first) <= 24);
        CHECK(extent_z(first) <= 24);

        const Footprint first_footprint = footprint_of(a, first);
        for (std::size_t j = i + 1; j < kFactoryKitLayout.size(); ++j) {
            const KitPlacement& b = kFactoryKitLayout[j];
            CHECK(a.kind != b.kind);
            const Kit second = generate_machine_kit(b.kind, b.seed);
            CHECK(!overlaps(first_footprint, footprint_of(b, second)));
        }
    }
}

TEST(factory_layout_stamps_every_kit_above_the_floor) {
    constexpr i32 floor_y = 3;
    World world;
    place_factory_kits(world, floor_y);

    for (const KitPlacement& placement : kFactoryKitLayout) {
        const Kit kit = generate_machine_kit(placement.kind, placement.seed);
        bool matches = true;
        for (i32 y = 0; y < extent_y(kit); ++y) {
            for (i32 z = 0; z < extent_z(kit); ++z) {
                for (i32 x = 0; x < extent_x(kit); ++x) {
                    const MaterialId actual = world.voxel_at(
                        placement.x + x, floor_y + 1 + y, placement.z + z);
                    if (actual != expected_voxel(kit, x, y, z)) {
                        matches = false;
                    }
                }
            }
        }
        CHECK(matches);
    }
    CHECK(world.voxel_at(0, floor_y, 0) == kMaterialAir);
}

TEST(factory_layout_is_deterministic) {
    World first;
    World second;
    place_factory_kits(first);
    place_factory_kits(second);

    for (const KitPlacement& placement : kFactoryKitLayout) {
        const Kit kit = generate_machine_kit(placement.kind, placement.seed);
        bool matches = true;
        for (i32 y = 0; y < extent_y(kit); ++y) {
            for (i32 z = 0; z < extent_z(kit); ++z) {
                for (i32 x = 0; x < extent_x(kit); ++x) {
                    if (first.voxel_at(placement.x + x, 1 + y, placement.z + z) !=
                        second.voxel_at(placement.x + x, 1 + y, placement.z + z)) {
                        matches = false;
                    }
                }
            }
        }
        CHECK(matches);
    }
}

TEST(factory_kits_reach_the_world_mesher) {
    World world;
    place_factory_kits(world);

    std::size_t quads = 0;
    for (const ChunkCoord coord : world.coords()) {
        quads += world.mesh(coord).quad_count();
    }
    CHECK(quads > 0);
}
