#pragma once
// A 32^3 chunk of voxels with a per-chunk material palette. Storing one byte
// per voxel plus a palette keeps a chunk at 32 KiB before compression; a
// factory floor is mostly air, so a run-length layer on top is the next win.
#include "core/types.hpp"

#include <array>

namespace sc {

inline constexpr int kChunkSize = 32;
inline constexpr int kChunkVolume = kChunkSize * kChunkSize * kChunkSize;
using MaterialId = u16;
inline constexpr MaterialId kMaterialAir = 0;

class VoxelChunk {
public:
    VoxelChunk();

    static constexpr int index_of(int x, int y, int z) {
        return x + kChunkSize * (y + kChunkSize * z);
    }

    static constexpr bool in_bounds(int x, int y, int z) {
        return x >= 0 && y >= 0 && z >= 0 && x < kChunkSize && y < kChunkSize && z < kChunkSize;
    }

    MaterialId at(int x, int y, int z) const {
        if (!in_bounds(x, y, z)) {
            return kMaterialAir;
        }
        return palette_[indices_[static_cast<std::size_t>(index_of(x, y, z))]];
    }

    bool filled(int x, int y, int z) const { return at(x, y, z) != kMaterialAir; }

    // Returns false if the chunk's palette is full and the material is new.
    bool set(int x, int y, int z, MaterialId material);

    u32 palette_size() const { return palette_size_; }
    const std::array<MaterialId, 256>& palette() const { return palette_; }

    static constexpr u32 kMaxPaletteEntries = 256;

private:
    std::array<MaterialId, 256> palette_{};
    u32 palette_size_ = 1;  // entry 0 is always air
    std::array<u8, kChunkVolume> indices_{};

    int find_or_add(MaterialId material);
};

}  // namespace sc
