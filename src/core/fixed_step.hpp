#pragma once
// Fixed-timestep clock. The simulation only ever advances in whole ticks, so a
// replay of the same inputs produces the same factory on every machine. Render
// interpolation uses alpha(); simulation code must never see wall-clock time.
#include "core/types.hpp"

#include <cmath>

namespace sc {

class FixedStepClock {
public:
    explicit FixedStepClock(f64 rate_hz = 60.0) : dt_us_(1000000.0 / rate_hz) {}

    // Feeds elapsed wall time in and calls onTick(tick_index) once per whole
    // tick. Returns the number of ticks run. A long frame is clamped so the
    // process cannot enter a spiral of death; the leftover is discarded.
    template <typename TickFn>
    u64 advance(f64 elapsed_seconds, TickFn&& on_tick) {
        constexpr u64 kMaxTicksPerCall = 8;
        constexpr f64 kEpsilonUs = 1e-6;

        acc_us_ += elapsed_seconds * 1000000.0;
        u64 ticks = 0;
        while (acc_us_ + kEpsilonUs >= dt_us_) {
            if (ticks >= kMaxTicksPerCall) {
                acc_us_ = std::fmod(acc_us_, dt_us_);
                break;
            }
            acc_us_ -= dt_us_;
            ++tick_count_;
            ++ticks;
            on_tick(tick_count_);
        }
        return ticks;
    }

    f64 rate_hz() const { return 1000000.0 / dt_us_; }
    f64 alpha() const { return acc_us_ / dt_us_; }
    u64 tick_count() const { return tick_count_; }

private:
    f64 dt_us_ = 0.0;
    f64 acc_us_ = 0.0;
    u64 tick_count_ = 0;
};

}  // namespace sc
