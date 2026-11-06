#include "assets/material_library.hpp"

#include <utility>

namespace sc {

MaterialId MaterialLibrary::add(std::string name, f32 r, f32 g, f32 b, f32 roughness,
                                f32 metallic, bool emissive) {
    Material material;
    material.id = static_cast<MaterialId>(materials_.size() + 1);
    material.name = std::move(name);
    material.base_r = r;
    material.base_g = g;
    material.base_b = b;
    material.roughness = roughness;
    material.metallic = metallic;
    material.emissive = emissive;
    materials_.push_back(std::move(material));
    return materials_.back().id;
}

const Material* MaterialLibrary::find(MaterialId id) const {
    if (id == kMaterialAir || id > materials_.size()) {
        return nullptr;
    }
    return &materials_[id - 1];
}

MaterialLibrary MaterialLibrary::create_factory_defaults() {
    MaterialLibrary library;
    library.add("concrete_floor", 0.42f, 0.42f, 0.44f, 0.92f, 0.0f);
    library.add("steel_painted", 0.30f, 0.33f, 0.36f, 0.55f, 0.65f);
    library.add("hazard_yellow", 0.88f, 0.72f, 0.12f, 0.60f, 0.05f);
    library.add("machine_grey", 0.55f, 0.57f, 0.60f, 0.70f, 0.20f);
    library.add("safety_glass", 0.60f, 0.78f, 0.82f, 0.10f, 0.0f);
    library.add("indicator_green", 0.20f, 0.85f, 0.35f, 0.30f, 0.0f, true);
    library.add("indicator_red", 0.90f, 0.20f, 0.20f, 0.30f, 0.0f, true);
    // Appended, never inserted: an id is the index into this list and a chunk
    // stores ids, so a new material in the middle would repaint an existing plant.
    library.add("brushed_steel", 0.62f, 0.64f, 0.66f, 0.35f, 0.85f);
    library.add("rubber_black", 0.10f, 0.10f, 0.11f, 0.95f, 0.0f);
    library.add("copper_bus", 0.72f, 0.42f, 0.20f, 0.40f, 0.90f);
    library.add("guard_mesh", 0.48f, 0.50f, 0.52f, 0.75f, 0.55f);
    return library;
}

}  // namespace sc
