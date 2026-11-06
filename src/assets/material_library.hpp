#pragma once
// Procedural material definitions. No texture files exist in this repository:
// materials are a handful of scalars that the shader turns into surface detail.
#include "core/types.hpp"
#include "world/voxel_chunk.hpp"

#include <string>
#include <vector>

namespace sc {

struct Material {
    MaterialId id = kMaterialAir;
    std::string name;
    f32 base_r = 0.5f;
    f32 base_g = 0.5f;
    f32 base_b = 0.5f;
    f32 roughness = 0.8f;
    f32 metallic = 0.0f;
    bool emissive = false;
};

class MaterialLibrary {
public:
    // Id 0 is reserved for air. The first real material gets id 1, and ids are
    // the index plus one: a new material is appended, because inserting one would
    // repaint every chunk that already stores a later id.
    MaterialId add(std::string name, f32 r, f32 g, f32 b, f32 roughness, f32 metallic,
                   bool emissive = false);

    const Material* find(MaterialId id) const;
    std::size_t size() const { return materials_.size(); }
    const std::vector<Material>& materials() const { return materials_; }

    static MaterialLibrary create_factory_defaults();

private:
    std::vector<Material> materials_;
};

}  // namespace sc
