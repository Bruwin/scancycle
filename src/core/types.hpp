#pragma once
// Fixed-width aliases used across the engine. Nothing in Scan Cycle should use
// a bare int where the width matters: the PLC runtime and the voxel world are
// both bit-exact by design.
#include <cstddef>
#include <cstdint>

namespace sc {

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

using i8 = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;

using f32 = float;
using f64 = double;

}  // namespace sc
