#pragma once
// Internal seam for the kit grammars. Callers use generate_machine_kit() from
// kit_generator.hpp; this header keeps the class-specific builders out of the
// public asset API.
#include "assets/kit_generator.hpp"
#include "core/deterministic_rng.hpp"

namespace sc::detail {

void add_kit_pieces(Kit& kit, Pcg32& rng);

}  // namespace sc::detail
