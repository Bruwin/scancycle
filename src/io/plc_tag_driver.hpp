#pragma once
// A libplctag-backed driver, separated from the C library by a narrow API
// interface. That seam is deliberate: the latching, changed-output and
// commissioning rules can be tested without a PLC or the optional dependency,
// while the real adapter is only one small translation unit.
#include "core/types.hpp"
#include "io/commissioning.hpp"
#include "io/driver.hpp"
#include "plc/vm.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace sc::io {

class PlcTagApi {
public:
    virtual ~PlcTagApi() = default;

    // Returns a positive handle or a negative error. `tag` is a complete
    // libplctag attribute string, so the caller chooses the protocol and PLC.
    virtual i32 create(std::string_view tag, int timeout_ms, std::string& error) = 0;
    virtual bool read(i32 handle, int timeout_ms, std::string& error) = 0;
    virtual bool get_bool(i32 handle, bool& value, std::string& error) = 0;
    virtual bool set_bool(i32 handle, bool value, std::string& error) = 0;
    virtual bool write(i32 handle, int timeout_ms, std::string& error) = 0;
    virtual void destroy(i32 handle) = 0;
};

enum class PlcTagDirection : u8 { Input = 0, Output };

struct PlcTagBinding {
    std::string name;
    std::string tag;
    PlcTagDirection direction = PlcTagDirection::Input;
};

struct LibPlcTagConfig {
    int timeout_ms = 1000;
    std::vector<PlcTagBinding> tags;

    // Required only when an output binding is present. A null policy fails
    // closed: reads still work, but no output is written.
    CommissioningPolicy* commissioning = nullptr;
};

class LibPlcTagDriver final : public PlcDriver {
public:
    LibPlcTagDriver(LibPlcTagConfig config, plc::Vm& vm,
                    std::unique_ptr<PlcTagApi> api);
    ~LibPlcTagDriver() override;

    LibPlcTagDriver(const LibPlcTagDriver&) = delete;
    LibPlcTagDriver& operator=(const LibPlcTagDriver&) = delete;

    bool connect() override;
    void disconnect() override;
    bool connected() const override { return connected_; }
    bool read_inputs() override;
    bool write_outputs() override;

    const char* name() const override { return "libplctag"; }
    std::string last_error() const override { return error_; }

    bool run_scan(i64 now_ms);
    i64 scans() const { return scans_; }

private:
    bool write_outputs_at(i64 now_ms);

    LibPlcTagConfig config_;
    plc::Vm& vm_;
    std::unique_ptr<PlcTagApi> api_;
    std::vector<i32> handles_;
    std::vector<u8> written_;
    std::vector<u8> last_written_;
    bool connected_ = false;
    i64 scans_ = 0;
    i64 last_scan_ms_ = 0;
    std::string error_;
};

}  // namespace sc::io
