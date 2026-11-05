#include "world/mesh.hpp"

#include "world/voxel_chunk.hpp"

#include <array>

namespace sc {
namespace {

struct MaskCell {
    u16 material = kMaterialAir;
};

}  // namespace

void MeshData::clear() {
    vertices.clear();
    indices.clear();
}

MeshData greedy_mesh(const VoxelChunk& chunk) {
    MeshData out;
    constexpr int N = kChunkSize;

    for (int axis = 0; axis < 3; ++axis) {
        const int plane_u = (axis + 1) % 3;
        const int plane_v = (axis + 2) % 3;

        for (int side = 0; side < 2; ++side) {
            const int sign = (side == 0) ? 1 : -1;
            const u8 face = static_cast<u8>(axis * 2 + side);

            for (int slice = 0; slice < N; ++slice) {
                std::array<MaskCell, N * N> mask{};
                bool any = false;

                for (int j = 0; j < N; ++j) {
                    for (int i = 0; i < N; ++i) {
                        int p[3] = {0, 0, 0};
                        p[axis] = slice;
                        p[plane_u] = i;
                        p[plane_v] = j;

                        const MaterialId here = chunk.at(p[0], p[1], p[2]);
                        if (here == kMaterialAir) {
                            continue;
                        }
                        int q[3] = {p[0], p[1], p[2]};
                        q[axis] += sign;
                        if (chunk.at(q[0], q[1], q[2]) != kMaterialAir) {
                            continue;
                        }
                        mask[static_cast<std::size_t>(i + N * j)].material = here;
                        any = true;
                    }
                }

                if (!any) {
                    continue;
                }

                for (int j = 0; j < N; ++j) {
                    for (int i = 0; i < N;) {
                        const u16 material = mask[static_cast<std::size_t>(i + N * j)].material;
                        if (material == kMaterialAir) {
                            ++i;
                            continue;
                        }

                        int width = 1;
                        while (i + width < N &&
                               mask[static_cast<std::size_t>(i + width + N * j)].material ==
                                   material) {
                            ++width;
                        }

                        int height = 1;
                        bool growing = true;
                        while (j + height < N && growing) {
                            for (int k = 0; k < width; ++k) {
                                if (mask[static_cast<std::size_t>(i + k + N * (j + height))]
                                        .material != material) {
                                    growing = false;
                                    break;
                                }
                            }
                            if (growing) {
                                ++height;
                            }
                        }

                        for (int jj = 0; jj < height; ++jj) {
                            for (int ii = 0; ii < width; ++ii) {
                                mask[static_cast<std::size_t>(i + ii + N * (j + jj))].material =
                                    kMaterialAir;
                            }
                        }

                        const u32 base = static_cast<u32>(out.vertices.size());
                        auto add_vertex = [&](int uu, int vv) {
                            int p[3] = {0, 0, 0};
                            p[axis] = (sign > 0) ? slice + 1 : slice;
                            p[plane_u] = uu;
                            p[plane_v] = vv;
                            out.vertices.push_back(PackedVertex{
                                static_cast<u8>(p[0]), static_cast<u8>(p[1]),
                                static_cast<u8>(p[2]), face, 255, 0, material});
                        };

                        // Positive faces wind one way, negative faces the other,
                        // so both end up counter-clockwise when seen from
                        // outside the solid.
                        if (sign > 0) {
                            add_vertex(i, j);
                            add_vertex(i + width, j);
                            add_vertex(i + width, j + height);
                            add_vertex(i, j + height);
                        } else {
                            add_vertex(i, j);
                            add_vertex(i, j + height);
                            add_vertex(i + width, j + height);
                            add_vertex(i + width, j);
                        }

                        out.indices.push_back(base + 0);
                        out.indices.push_back(base + 1);
                        out.indices.push_back(base + 2);
                        out.indices.push_back(base + 0);
                        out.indices.push_back(base + 2);
                        out.indices.push_back(base + 3);

                        i += width;
                    }
                }
            }
        }
    }

    return out;
}

}  // namespace sc
