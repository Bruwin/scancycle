#include "io/commissioning.hpp"

#include <utility>

namespace sc::io {

void CommissioningPolicy::allow_address(std::string address) {
    for (const std::string& existing : allowlist_) {
        if (existing == address) {
            return;
        }
    }
    allowlist_.push_back(std::move(address));
}

bool CommissioningPolicy::is_allowed(std::string_view address) const {
    for (const std::string& existing : allowlist_) {
        if (existing == address) {
            return true;
        }
    }
    return false;
}

bool CommissioningPolicy::begin_session(i64 now_ms, i64 duration_ms) {
    if (duration_ms <= 0) {
        return false;
    }
    session_open_ = true;
    session_end_ms_ = now_ms + duration_ms;
    return true;
}

void CommissioningPolicy::end_session() {
    session_open_ = false;
    session_end_ms_ = 0;
}

bool CommissioningPolicy::session_active(i64 now_ms) const {
    return session_open_ && now_ms < session_end_ms_;
}

void CommissioningPolicy::set_min_write_interval_ms(i64 interval_ms) {
    min_write_interval_ms_ = interval_ms < 0 ? 0 : interval_ms;
}

bool CommissioningPolicy::can_write(std::string_view address, i64 now_ms) {
    if (!session_active(now_ms) || !is_allowed(address)) {
        ++denied_;
        return false;
    }
    if (last_write_ms_ >= 0 && now_ms - last_write_ms_ < min_write_interval_ms_) {
        ++denied_;
        return false;
    }
    last_write_ms_ = now_ms;
    ++permitted_;
    return true;
}

}  // namespace sc::io
