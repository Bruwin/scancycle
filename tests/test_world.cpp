#include "harness.hpp"
#include "world/world.hpp"

#include "world/mesh.hpp"
#include "world/voxel_chunk.hpp"

namespace {

void fill_solid(sc::VoxelChunk& chunk) {
    for (int z = 0; z < sc::kChunkSize; ++z) {
        for (int y = 0; y < sc::kChunkSize; ++y) {
            for (int x = 0; x < sc::kChunkSize; ++x) {
                chunk.set(x, y, z, 1);
            }
        }
    }
}

}  // namespace

TEST(chunk_stores_materials_by_palette) {
    sc::VoxelChunk chunk;
    CHECK(chunk.palette_size() == 1);
    CHECK(chunk.at(0, 0, 0) == sc::kMaterialAir);

    chunk.set(0, 0, 0, 7);
    chunk.set(1, 0, 0, 7);
    chunk.set(2, 0, 0, 9);

    CHECK(chunk.palette_size() == 3);
    CHECK(chunk.at(0, 0, 0) == 7);
    CHECK(chunk.at(1, 0, 0) == 7);
    CHECK(chunk.at(2, 0, 0) == 9);
    CHECK(chunk.at(3, 0, 0) == sc::kMaterialAir);
    CHECK(chunk.at(-1, 0, 0) == sc::kMaterialAir);
}

TEST(solid_chunk_meshes_to_six_greedy_quads) {
    sc::VoxelChunk chunk;
    fill_solid(chunk);
    const sc::MeshData mesh = sc::greedy_mesh(chunk);

    CHECK(mesh.quad_count() == 6);
    CHECK(mesh.vertices.size() == 24);
    CHECK(mesh.indices.size() == 36);
}

TEST(single_voxel_meshes_to_six_quads) {
    sc::VoxelChunk chunk;
    chunk.set(5, 5, 5, 3);
    const sc::MeshData mesh = sc::greedy_mesh(chunk);

    CHECK(mesh.quad_count() == 6);
    CHECK(mesh.vertices.size() == 24);
}

TEST(two_adjacent_voxels_share_their_inner_faces) {
    sc::VoxelChunk chunk;
    chunk.set(5, 5, 5, 1);
    chunk.set(6, 5, 5, 1);
    const sc::MeshData mesh = sc::greedy_mesh(chunk);

    // Six quads, not twelve: the shared face is culled and the outer faces
    // merge into 1x2 rectangles.
    CHECK(mesh.quad_count() == 6);
}

TEST(different_materials_do_not_merge) {
    sc::VoxelChunk chunk;
    chunk.set(5, 5, 5, 1);
    chunk.set(6, 5, 5, 2);
    const sc::MeshData mesh = sc::greedy_mesh(chunk);

    // The shared face is still culled; only the merged outer faces split,
    // so 10 exposed faces become 10 quads instead of 6.
    CHECK(mesh.quad_count() == 10);
}

TEST(empty_chunk_meshes_to_nothing) {
    sc::VoxelChunk chunk;
    const sc::MeshData mesh = sc::greedy_mesh(chunk);
    CHECK(mesh.empty());
    CHECK(mesh.indices.empty());
}

TEST(a_solid_neighbour_hides_the_wall_between_chunks) {
    sc::VoxelChunk chunk;
    fill_solid(chunk);

    sc::ChunkNeighbours neighbours;
    for (auto& material : neighbours.slabs[sc::kFaceNegX]) {
        material = 1;
    }

    const sc::MeshData mesh = sc::greedy_mesh(chunk, neighbours);

    // Six walls normally; the one shared with the neighbour is not drawn at all.
    CHECK(mesh.quad_count() == 5);
    for (const sc::PackedVertex& vertex : mesh.vertices) {
        CHECK(vertex.face != sc::kFaceNegX);
    }
}

TEST(solid_chunk_surrounded_by_solid_chunks_meshes_to_nothing) {
    sc::VoxelChunk chunk;
    fill_solid(chunk);

    sc::ChunkNeighbours neighbours;
    for (auto& slab : neighbours.slabs) {
        for (auto& material : slab) {
            material = 1;
        }
    }

    CHECK(sc::greedy_mesh(chunk, neighbours).empty());
}

TEST(meshing_through_the_world_culls_the_seam_between_chunks) {
    sc::World world;
    for (int z = 0; z < sc::kChunkSize; ++z) {
        for (int x = 0; x < 2 * sc::kChunkSize; ++x) {
            world.set_voxel(x, 0, z, 1);
        }
    }

    // A lone floor slab costs six quads. Each half of a two-chunk floor has the
    // wall at the seam culled, because there is solid ground on both sides.
    sc::VoxelChunk alone;
    for (int z = 0; z < sc::kChunkSize; ++z) {
        for (int x = 0; x < sc::kChunkSize; ++x) {
            alone.set(x, 0, z, 1);
        }
    }
    CHECK(sc::greedy_mesh(alone).quad_count() == 6);

    CHECK(world.mesh({0, 0, 0}).quad_count() == 5);
    CHECK(world.mesh({1, 0, 0}).quad_count() == 5);
}
