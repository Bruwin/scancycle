#pragma once
// Live DF1 driver: one boolean tag per bit of a 16-bit data-table word. Inputs
// are latched into the VM before the scan; only changed outputs are written
// after it, and every output write is a read-modify-write of its word behind
// the CommissioningPolicy gate, so neighbouring bits are never clobbered. The
// transport is injected (io/df1_link), which keeps serial-port code out of the
// scan contract and lets the whole driver run against the loopback peer.
#include "core/types.hpp"
#include "io/commissioning.hpp"
#include "io/df1.hpp"
#include "io/df1_client.hpp"
#include "io/df1_pccc.hpp"
#include "io/driver.hpp"
#include "plc/vm.hpp"

#include <string>
#include <vector>

namespace sc::io {

enum class Df1Direction : u8 { Input = 0, Output };

struct Df1Tag {
    std::string name;
    u8 file = 7;
    u16 element = 0;
    u8 bit = 0;  // 0..15 inside the 16-bit word
    u8 file_type = kPcccFileTypeInteger;
    Df1Direction direction = Df1Direction::Input;
};

struct Df1Config {
    u8 station = 1;      // this node's address on the link
    u8 destination = 0;  // the controller's address
    Df1Checksum checksum = Df1Checksum::Bcc;
    std::vector<Df1Tag> tags;

    // Required only when an output tag is present. A null policy fails closed:
    // the driver may still read, but it will refuse to write.
    CommissioningPolicy* commissioning = nullptr;
};

class Df1Driver final : public PlcDriver {
public:
    Df1Driver(Df1Config config, plc::Vm& vm, Df1Link& link);
    ~Df1Driver() override;

    Df1Driver(const Df1Driver&) = delete;
    Df1Driver& operator=(const Df1Driver&) = delete;

    bool connect() override;
    void disconnect() override;
    bool connected() const override { return connected_; }
    bool read_inputs() override;
    bool write_outputs() override;

    const char* name() const override { return "df1"; }
    std::string last_error() const override { return error_; }

    // The scan contract every live driver must share with SimDriver.
    bool run_scan(i64 now_ms);
    i64 scans() const { return scans_; }
    i64 exchanges() const { return client_.exchanges(); }

private:
    bool read_word(const Df1Tag& tag, u16& value);
    bool write_word(const Df1Tag& tag, u16 value);
    bool write_outputs_at(i64 now_ms);

    Df1Config config_;
    plc::Vm& vm_;
    Df1Client client_;
    std::vector<u8> written_;
    std::vector<u8> last_written_;
    i64 scans_ = 0;
    i64 last_scan_ms_ = 0;
    bool connected_ = false;
    std::string error_;
};

}  // namespace sc::io
