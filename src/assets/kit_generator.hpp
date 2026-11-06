#pragma once
// Parametric machine kits. A kit is a handful of boxes with materials, chosen by
// a seeded grammar rather than noise, because a student must be able to identify
// a photo-eye at twenty metres. The class fixes the silhouette; the seed only
// varies proportions and optional fittings.
#include "core/types.hpp"
#include "world/voxel_chunk.hpp"

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sc {

enum class KitClass : u8 {
    Conveyor = 0,
    Press,
    ControlCabinet,
    PhotoEye,
    StackLight,
    SafetyFence,
    PalletRack,
    PipeRun,
    Barrel,
    PalletLoad,
};

inline constexpr std::array<KitClass, 10> kKitCatalog{
    KitClass::Conveyor,
    KitClass::Press,
    KitClass::ControlCabinet,
    KitClass::PhotoEye,
    KitClass::StackLight,
    KitClass::SafetyFence,
    KitClass::PalletRack,
    KitClass::PipeRun,
    KitClass::Barrel,
    KitClass::PalletLoad,
};

// These ids follow the voxel shader's industrial palette. The CPU-side
// MaterialLibrary is a separate catalogue and is not uploaded by the renderer
// yet, so kit colours must stay aligned with chunk.frag.
enum class KitMaterial : MaterialId {
    MachineBlue = 1,
    SafetyYellow = 2,
    Steel = 3,
    EStopRed = 4,
    ConveyorGreen = 5,
    PaintedWhite = 6,
    Rubber = 7,
    Timber = 8,
    Dark = 9,
};

struct KitPiece {
    MaterialId material = static_cast<MaterialId>(KitMaterial::Steel);
    i32 x = 0;
    i32 y = 0;
    i32 z = 0;
    u32 width = 1;
    u32 height = 1;
    u32 depth = 1;
};

struct Kit {
    KitClass kind = KitClass::Conveyor;
    std::string name;
    std::vector<KitPiece> pieces;
};

std::string_view kit_class_name(KitClass kind);
std::optional<KitClass> kit_class_from_name(std::string_view name);

// Deterministic for a given (class, seed) pair.
Kit generate_machine_kit(KitClass kind, u64 seed);

}  // namespace sc
