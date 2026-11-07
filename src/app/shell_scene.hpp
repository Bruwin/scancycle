#pragma once
// A compact JSON projection of the static factory kit layout for the 1.0
// shell's browser canvas. The native Vulkan viewer remains the production
// renderer; this exists so the shell can show the same procedural cell without
// inventing a second, hand-authored scene.
#include <string>

namespace sc::app {

std::string factory_scene_json();

}  // namespace sc::app
