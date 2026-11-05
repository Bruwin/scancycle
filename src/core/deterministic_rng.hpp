#pragma once
// PCG32 (XSH-RR). Chosen because it is small, fast, has a provable period of
// 2^64 per stream and is trivially reproducible across compilers and
// platforms. Simulation code must use this and nothing else: no rand(), no
// std::random_device, no time-seeded generators.
#include "core/types.hpp"

namespace sc {

class Pcg32 {
public:
    explicit Pcg32(u64 seed = 0x853c49e6748fea9bULL, u64 stream = 0xda3e39cb94b95bdbULL) {
        reseed(seed, stream);
    }

    void reseed(u64 seed, u64 stream) {
        state_ = 0u;
        inc_ = (stream << 1u) | 1u;
        next_u32();
        state_ += seed;
        next_u32();
    }

    u32 next_u32() {
        const u64 old = state_;
        state_ = old * 6364136223846793005ULL + inc_;
        const u32 xorshifted = static_cast<u32>(((old >> 18u) ^ old) >> 27u);
        const u32 rot = static_cast<u32>(old >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((~rot + 1u) & 31u));
    }

    // Uniform in [0, bound). Lemire-style rejection keeps this unbiased, which
    // matters because procedural content is compared between machines.
    u32 bounded(u32 bound) {
        if (bound == 0u) {
            return 0u;
        }
        const u32 threshold = (0u - bound) % bound;
        for (;;) {
            const u32 r = next_u32();
            if (r >= threshold) {
                return r % bound;
            }
        }
    }

    f32 unit_f32() { return static_cast<f32>(next_u32() >> 8) * (1.0f / 16777216.0f); }

    u64 state() const { return state_; }

private:
    u64 state_ = 0;
    u64 inc_ = 1;
};

}  // namespace sc
