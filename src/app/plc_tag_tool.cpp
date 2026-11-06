#include "app/plc_tag_tool.hpp"

#include <utility>

namespace sc::app {
namespace {

class TagHandle {
public:
    TagHandle(io::PlcTagApi& api, i32 handle) : api_(api), handle_(handle) {}
    ~TagHandle() {
        if (handle_ > 0) {
            api_.destroy(handle_);
        }
    }

    TagHandle(const TagHandle&) = delete;
    TagHandle& operator=(const TagHandle&) = delete;

    i32 get() const { return handle_; }

private:
    io::PlcTagApi& api_;
    i32 handle_;
};

bool validate(const PlcTagToolConfig& config, std::string& error) {
    if (config.tag.empty()) {
        error = "a complete libplctag tag string is required";
        return false;
    }
    if (config.timeout_ms <= 0) {
        error = "timeout must be positive";
        return false;
    }
    return true;
}

bool create_tag(const PlcTagToolConfig& config, io::PlcTagApi& api, i32& handle,
                std::string& error) {
    std::string detail;
    handle = api.create(config.tag, config.timeout_ms, detail);
    if (handle > 0) {
        return true;
    }
    error = "could not create tag";
    if (!detail.empty()) {
        error += ": " + detail;
    }
    return false;
}

bool gate_write(const PlcTagToolConfig& config, io::CommissioningPolicy& local,
                io::CommissioningPolicy*& policy, std::string& error) {
    policy = config.commissioning;
    if (policy == nullptr) {
        local.allow_address(config.tag);
        local.set_min_write_interval_ms(0);
        if (!local.begin_session(0, 60'000)) {
            error = "could not open a commissioning session";
            return false;
        }
        policy = &local;
    }
    if (!policy->can_write(config.tag, 0)) {
        error = "commissioning gate denied write to '" + config.tag + "'";
        return false;
    }
    return true;
}

bool read_value(const PlcTagToolConfig& config, io::PlcTagApi& api, i32 handle,
                bool& value, std::string& error) {
    std::string detail;
    if (!api.read(handle, config.timeout_ms, detail)) {
        error = "could not read tag";
        if (!detail.empty()) {
            error += ": " + detail;
        }
        return false;
    }
    if (!api.get_bool(handle, value, detail)) {
        error = "could not decode tag";
        if (!detail.empty()) {
            error += ": " + detail;
        }
        return false;
    }
    return true;
}

}  // namespace

PlcTagToolResult plc_tag_read(const PlcTagToolConfig& config, io::PlcTagApi& api) {
    PlcTagToolResult result;
    if (!validate(config, result.error)) {
        return result;
    }

    i32 handle = 0;
    if (!create_tag(config, api, handle, result.error)) {
        return result;
    }
    TagHandle tag(api, handle);
    if (!read_value(config, api, tag.get(), result.value, result.error)) {
        return result;
    }
    result.ok = true;
    return result;
}

PlcTagToolResult plc_tag_write(const PlcTagToolConfig& config, io::PlcTagApi& api,
                               bool value) {
    PlcTagToolResult result;
    if (!validate(config, result.error)) {
        return result;
    }

    io::CommissioningPolicy local;
    io::CommissioningPolicy* policy = nullptr;
    if (!gate_write(config, local, policy, result.error)) {
        return result;
    }

    i32 handle = 0;
    if (!create_tag(config, api, handle, result.error)) {
        return result;
    }
    TagHandle tag(api, handle);

    bool current = false;
    if (!read_value(config, api, tag.get(), current, result.error)) {
        result.error = "could not read tag before write: " + result.error;
        return result;
    }

    std::string detail;
    if (!api.set_bool(tag.get(), value, detail)) {
        result.error = "could not stage tag value";
        if (!detail.empty()) {
            result.error += ": " + detail;
        }
        return result;
    }
    if (!api.write(tag.get(), config.timeout_ms, detail)) {
        result.error = "could not write tag";
        if (!detail.empty()) {
            result.error += ": " + detail;
        }
        return result;
    }

    result.value = value;
    result.ok = true;
    return result;
}

}  // namespace sc::app
