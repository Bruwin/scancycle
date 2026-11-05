#pragma once
// Safety gate for writing to physical hardware. Deny by default: a write is only
// permitted while a commissioning session is open, the address is explicitly
// allowlisted, and the rate limit has elapsed. The simulation never writes to
// hardware on its own; this is the only path that can.
#include "core/types.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace sc::io {

class CommissioningPolicy {
public:
    void allow_address(std::string address);
    bool is_allowed(std::string_view address) const;

    bool begin_session(i64 now_ms, i64 duration_ms);
    void end_session();
    bool session_active(i64 now_ms) const;

    void set_min_write_interval_ms(i64 interval_ms);

    // Records the decision for telemetry and returns whether the write may go
    // out. Calling this is mandatory before every physical write.
    bool can_write(std::string_view address, i64 now_ms);

    i64 writes_permitted() const { return permitted_; }
    i64 writes_denied() const { return denied_; }
    std::size_t allowlist_size() const { return allowlist_.size(); }

private:
    std::vector<std::string> allowlist_;
    bool session_open_ = false;
    i64 session_end_ms_ = 0;
    i64 last_write_ms_ = -1;
    i64 min_write_interval_ms_ = 100;
    i64 permitted_ = 0;
    i64 denied_ = 0;
};

}  // namespace sc::io
