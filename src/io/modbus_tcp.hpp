#pragma once
// Live Modbus/TCP driver: one boolean tag per holding register. Inputs are
// latched into the VM before the scan; changed outputs are written after it.
// Reads are unrestricted, but every field write passes through the explicit
// CommissioningPolicy gate. No socket layer is hidden in the codec.
#include "core/types.hpp"
#include "io/commissioning.hpp"
#include "io/driver.hpp"
#include "io/socket_io.hpp"
#include "plc/vm.hpp"

#include <string>
#include <vector>

namespace sc::io {

enum class ModbusTagDirection : u8 { Input = 0, Output };

struct ModbusTag {
    std::string name;
    u16 address = 0;
    ModbusTagDirection direction = ModbusTagDirection::Input;
};

struct ModbusTcpConfig {
    std::string host = "127.0.0.1";
    u16 port = 502;
    u8 unit = 1;
    int timeout_ms = 1000;
    std::vector<ModbusTag> tags;

    // Required only when an output tag is present. A null policy fails closed:
    // the driver may still read, but it will refuse to write.
    CommissioningPolicy* commissioning = nullptr;
};

class ModbusTcpDriver final : public PlcDriver {
public:
    ModbusTcpDriver(ModbusTcpConfig config, plc::Vm& vm);
    ~ModbusTcpDriver() override;

    ModbusTcpDriver(const ModbusTcpDriver&) = delete;
    ModbusTcpDriver& operator=(const ModbusTcpDriver&) = delete;

    bool connect() override;
    void disconnect() override;
    bool connected() const override;
    bool read_inputs() override;
    bool write_outputs() override;

    const char* name() const override { return "modbus-tcp"; }
    std::string last_error() const override { return error_; }

    // The scan contract every live driver must share with SimDriver.
    bool run_scan(i64 now_ms) override;
    i64 scans() const override { return scans_; }

private:
    u16 next_transaction();
    bool exchange(const std::vector<u8>& request, std::vector<u8>& response);
    bool read_register(u16 address, u16& value);
    bool write_register(u16 address, u16 value);
    bool write_outputs_at(i64 now_ms);

    ModbusTcpConfig config_;
    plc::Vm& vm_;
    SocketHandle socket_ = kInvalidSocket;
    u16 transaction_ = 0;
    i64 scans_ = 0;
    i64 last_scan_ms_ = 0;
    std::vector<u8> written_;
    std::vector<u8> last_written_;
    std::string error_;
};

}  // namespace sc::io
