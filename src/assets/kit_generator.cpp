#include "assets/kit_generator.hpp"

#include "assets/kit_grammar.hpp"
#include "core/deterministic_rng.hpp"

namespace sc {

std::string_view kit_class_name(KitClass kind) {
    switch (kind) {
        case KitClass::Conveyor:
            return "conveyor";
        case KitClass::Press:
            return "press";
        case KitClass::ControlCabinet:
            return "control_cabinet";
        case KitClass::PhotoEye:
            return "photo_eye";
        case KitClass::StackLight:
            return "stack_light";
        case KitClass::SafetyFence:
            return "safety_fence";
        case KitClass::PalletRack:
            return "pallet_rack";
        case KitClass::PipeRun:
            return "pipe_run";
        case KitClass::Barrel:
            return "barrel";
        case KitClass::PalletLoad:
            return "pallet_load";
    }
    return "conveyor";
}

std::optional<KitClass> kit_class_from_name(std::string_view name) {
    for (const KitClass kind : kKitCatalog) {
        if (kit_class_name(kind) == name) {
            return kind;
        }
    }
    return std::nullopt;
}

Kit generate_machine_kit(KitClass kind, u64 seed) {
    Pcg32 rng(seed, 0x6b17ULL);

    Kit kit;
    kit.kind = kind;
    kit.name = std::string(kit_class_name(kind));
    detail::add_kit_pieces(kit, rng);
    return kit;
}

}  // namespace sc
