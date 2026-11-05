#if !defined(_WIN32) && !defined(_XOPEN_SOURCE)
#define _XOPEN_SOURCE 700  // posix_openpt, grantpt, unlockpt, ptsname
#endif

#include "harness.hpp"

#include "io/df1_driver.hpp"
#include "io/df1_pccc.hpp"
#include "io/df1_profile.hpp"
#include "io/df1_serial_link.hpp"
#include "io/df1_slave.hpp"
#include "plc/compiler.hpp"
#include "plc/parser.hpp"

#include <atomic>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#if !defined(_WIN32)
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <unistd.h>
#endif

using namespace sc;

namespace {

const char* kWireProgram = R"st(PROGRAM Wire
VAR
    start : BOOL;
    run   : BOOL;
END_VAR
    run := start;
END_PROGRAM
)st";

bool compile_wire(plc::CompiledProgram& program) {
    plc::ParseResult parsed = plc::parse_program(kWireProgram);
    if (!parsed.program) {
        return false;
    }
    plc::CompileResult compiled = plc::compile(*parsed.program);
    if (!compiled.ok) {
        return false;
    }
    program = std::move(compiled.program);
    return true;
}

io::Df1Tag tag(std::string name, u16 element, u8 bit, io::Df1Direction direction) {
    io::Df1Tag out;
    out.name = std::move(name);
    out.file = 7;
    out.element = element;
    out.bit = bit;
    out.direction = direction;
    return out;
}

#if !defined(_WIN32)

struct PtyPair {
    int master = -1;
    std::string slave;

    bool open(std::string& error) {
        master = ::posix_openpt(O_RDWR | O_NOCTTY);
        if (master < 0) {
            error = "posix_openpt failed";
            return false;
        }
        if (::grantpt(master) != 0 || ::unlockpt(master) != 0) {
            error = "could not unlock the pseudo-terminal";
            return false;
        }
        const char* name = ::ptsname(master);
        if (name == nullptr) {
            error = "ptsname failed";
            return false;
        }
        slave = name;
        return true;
    }

    ~PtyPair() {
        if (master >= 0) {
            ::close(master);
        }
    }
};

#endif

}  // namespace

TEST(micro_logix_1000_profile_matches_the_factory_df1_defaults) {
    const io::Df1ControllerProfile& profile = io::kMicroLogix1000;
    CHECK(std::string(profile.name) == "MicroLogix 1000");
    CHECK(profile.controller_address == 1);
    CHECK(profile.host_address == 0);
    CHECK(profile.checksum == io::Df1Checksum::Crc16);
    CHECK(profile.baud == 19200);

    const io::Df1Config config = io::df1_config_for(profile);
    CHECK(config.destination == 1);
    CHECK(config.station == 0);
    CHECK(config.checksum == io::Df1Checksum::Crc16);
    CHECK(config.tags.empty());

    const io::Df1SerialConfig serial = io::df1_serial_config_for(profile, "/dev/ttyUSB0");
    CHECK(serial.device == "/dev/ttyUSB0");
    CHECK(serial.baud == 19200);
    CHECK(serial.checksum == io::Df1Checksum::Crc16);
    CHECK(serial.timeout_ms == 1000);
}

TEST(df1_serial_link_reports_a_missing_device) {
    io::Df1SerialConfig config;
    config.device = "/dev/scancycle-no-such-port";
    io::Df1SerialLink link(config);

    std::string error;
    CHECK(!link.open(error));
    CHECK(!error.empty());
}

#if !defined(_WIN32)

TEST(df1_serial_link_carries_a_micro_logix_scan_over_a_pseudo_terminal) {
    PtyPair pty;
    std::string error;
    CHECK(pty.open(error));
    if (pty.master < 0) {
        return;
    }

    io::Df1Slave peer;
    peer.add_word_file(7, io::kPcccFileTypeInteger, 8);
    peer.set_checksum(io::Df1Checksum::Crc16);
    CHECK(peer.set_bit(7, 0, 0, true));

    std::atomic<bool> finished{false};
    std::thread responder([&] {
        std::vector<u8> buffer;
        while (true) {
            pollfd entry{};
            entry.fd = pty.master;
            entry.events = POLLIN;
            const int ready = ::poll(&entry, 1, 50);
            if (ready <= 0) {
                if (finished.load()) {
                    break;
                }
                continue;
            }
            u8 chunk[128];
            const ssize_t count = ::read(pty.master, chunk, sizeof(chunk));
            if (count <= 0) {
                break;
            }
            buffer.insert(buffer.end(), chunk, chunk + count);

            std::vector<u8> body;
            std::size_t consumed = 0;
            std::string decode_error;
            if (!io::df1_decode(buffer, io::Df1Checksum::Crc16, body, consumed, decode_error)) {
                continue;
            }
            std::vector<u8> request(buffer.begin(), buffer.begin() + consumed);
            buffer.erase(buffer.begin(), buffer.begin() + consumed);

            std::vector<u8> reply;
            if (!peer.exchange(request, reply, decode_error) || reply.empty()) {
                break;
            }
            if (::write(pty.master, reply.data(), reply.size()) < 0) {
                break;
            }
        }
    });

    plc::CompiledProgram program;
    CHECK(compile_wire(program));
    plc::Vm vm(program);

    io::CommissioningPolicy policy;
    policy.allow_address("run");
    policy.set_min_write_interval_ms(0);
    CHECK(policy.begin_session(0, 10'000));

    io::Df1Config config = io::df1_config_for(io::kMicroLogix1000);
    config.tags = {
        tag("start", 0, 0, io::Df1Direction::Input),
        tag("run", 1, 0, io::Df1Direction::Output),
    };
    config.commissioning = &policy;

    io::Df1SerialLink link(io::df1_serial_config_for(io::kMicroLogix1000, pty.slave));
    io::Df1Driver driver(config, vm, link);
    CHECK(driver.connect());
    CHECK(driver.run_scan(0));

    driver.disconnect();
    finished.store(true);
    responder.join();

    // Join is the synchronisation point: the peer is idle and safe to inspect.
    CHECK(vm.get("run").as_bool());
    CHECK(peer.bit(7, 1, 0));
    CHECK(peer.writes() == 1);
    CHECK(policy.writes_permitted() == 1);
}

TEST(df1_serial_link_reports_a_silent_port) {
    PtyPair pty;
    std::string error;
    CHECK(pty.open(error));
    if (pty.master < 0) {
        return;
    }

    io::Df1SerialConfig config;
    config.device = pty.slave;
    config.timeout_ms = 100;
    io::Df1SerialLink link(config);
    CHECK(link.open(error));

    io::Df1Header header;
    header.command = io::kDf1CmdProtectedRead;
    std::vector<u8> body = io::df1_body(header);
    const std::vector<u8> payload =
        io::build_pccc_read_request(io::PcccAddress{7, io::kPcccFileTypeInteger, 0, 0}, 2);
    body.insert(body.end(), payload.begin(), payload.end());

    std::vector<u8> request;
    CHECK(io::df1_encode(body, io::Df1Checksum::Bcc, request, error));

    std::vector<u8> reply;
    CHECK(!link.exchange(request, reply, error));
    CHECK(error.find("no DF1 reply") != std::string::npos);
}

TEST(df1_serial_link_traces_both_directions_when_asked) {
    PtyPair pty;
    std::string error;
    CHECK(pty.open(error));
    if (pty.master < 0) {
        return;
    }

    io::Df1SerialConfig config;
    config.device = pty.slave;
    config.timeout_ms = 100;
    std::vector<u8> sent;
    std::vector<u8> received;
    config.trace = [&](bool transmitted, std::span<const u8> bytes) {
        std::vector<u8>& into = transmitted ? sent : received;
        into.insert(into.end(), bytes.begin(), bytes.end());
    };

    io::Df1SerialLink link(config);
    CHECK(link.open(error));

    // Noise ahead of the exchange proves the receive side is traced even when
    // no frame ever decodes.
    const u8 noise[] = {0xFFu, 0x00u, 0x10u};
    CHECK(::write(pty.master, noise, sizeof(noise)) ==
          static_cast<ssize_t>(sizeof(noise)));

    io::Df1Header header;
    header.command = io::kDf1CmdProtectedRead;
    std::vector<u8> body = io::df1_body(header);
    const std::vector<u8> payload =
        io::build_pccc_read_request(io::PcccAddress{7, io::kPcccFileTypeInteger, 0, 0}, 2);
    body.insert(body.end(), payload.begin(), payload.end());

    std::vector<u8> request;
    CHECK(io::df1_encode(body, io::Df1Checksum::Bcc, request, error));

    std::vector<u8> reply;
    CHECK(!link.exchange(request, reply, error));
    CHECK(sent == request);
    CHECK(received.size() >= 3);
    CHECK(received[0] == 0xFFu);
    CHECK(received[2] == 0x10u);
}

#endif
