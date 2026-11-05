#include "world/voxel_chunk.hpp"

namespace sc {

VoxelChunk::VoxelChunk() { palette_[0] = kMaterialAir; }

int VoxelChunk::find_or_add(MaterialId material) {
    for (u32 i = 0; i < palette_size_; ++i) {
        if (palette_[i] == material) {
            return static_cast<int>(i);
        }
    }
    if (palette_size_ >= kMaxPaletteEntries) {
        return -1;
    }
    palette_[palette_size_] = material;
    return static_cast<int>(palette_size_++);
}

bool VoxelChunk::set(int x, int y, int z, MaterialId material) {
    if (!in_bounds(x, y, z)) {
        return false;
    }
    const int slot = find_or_add(material);
    if (slot < 0) {
        return false;
    }
    indices_[static_cast<std::size_t>(index_of(x, y, z))] = static_cast<u8>(slot);
    return true;
}

}  // namespace sc
