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

// Now that the host acknowledges every reply, a peer that plays the controller
// has to consume link-layer control bytes before it looks for a frame.
void strip_control_bytes(std::vector<u8>& buffer) {
    while (buffer.size() >= 2 && buffer[0] == io::kDf1Dle) {
        if (buffer[1] != 0x06 && buffer[1] != 0x15) {
            break;
        }
        buffer.erase(buffer.begin(), buffer.begin() + 2);
    }
}

bool write_all(int fd, const std::vector<u8>& bytes) {
    std::size_t sent = 0;
    while (sent < bytes.size()) {
        const ssize_t count = ::write(fd, bytes.data() + sent, bytes.size() - sent);
        if (count <= 0) {
            return false;
        }
        sent += static_cast<std::size_t>(count);
    }
    return true;
}

// Collects what the host wrote, stopping once the line goes quiet.
std::vector<u8> drain_host_bytes(int fd) {
    std::vector<u8> bytes;
    for (int attempt = 0; attempt < 50; ++attempt) {
        pollfd entry{};
        entry.fd = fd;
        entry.events = POLLIN;
        const int ready = ::poll(&entry, 1, 10);
        if (ready <= 0) {
            if (!bytes.empty()) {
                break;
            }
            continue;
        }
        u8 chunk[128];
        const ssize_t count = ::read(fd, chunk, sizeof(chunk));
        if (count <= 0) {
            break;
        }
        bytes.insert(bytes.end(), chunk, chunk + count);
    }
    return bytes;
}

std::vector<u8> read_request_frame(io::Df1Checksum checksum) {
    io::Df1Header header;
    header.command = io::kDf1CmdProtectedRead;
    std::vector<u8> body = io::df1_body(header);
    const std::vector<u8> payload =
        io::build_pccc_read_request(io::PcccAddress{7, io::kPcccFileTypeInteger, 0, 0}, 2);
    body.insert(body.end(), payload.begin(), payload.end());
    std::vector<u8> frame;
    std::string error;
    if (!io::df1_encode(body, checksum, frame, error)) {
        return {};
    }
    return frame;
}

std::vector<u8> reply_frame(u8 transaction, io::Df1Checksum checksum) {
    io::Df1Header header;
    header.destination = 0;
    header.source = 1;
    header.command = io::kDf1CmdReadReply;
    header.transaction = transaction;
    const std::vector<u8> body = io::df1_body(header);
    std::vector<u8> frame;
    std::string error;
    if (!io::df1_encode(body, checksum, frame, error)) {
        return {};
    }
    return frame;
}

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

            strip_control_bytes(buffer);
            if (buffer.empty()) {
                continue;
            }

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

TEST(df1_serial_link_reports_a_nak_as_a_rejection) {
    PtyPair pty;
    std::string error;
    CHECK(pty.open(error));
    if (pty.master < 0) {
        return;
    }

    io::Df1SerialConfig config;
    config.device = pty.slave;
    config.timeout_ms = 300;
    io::Df1SerialLink link(config);
    CHECK(link.open(error));

    // Written after open: opening the port flushes whatever was already queued.
    CHECK(write_all(pty.master, {io::kDf1Dle, 0x15}));

    std::vector<u8> reply;
    CHECK(!link.exchange(read_request_frame(io::Df1Checksum::Bcc), reply, error));
    CHECK(error.find("NAK") != std::string::npos);
    CHECK(error.find("no DF1 reply") == std::string::npos);
}

TEST(df1_serial_link_accepts_a_reply_that_follows_an_ack) {
    PtyPair pty;
    std::string error;
    CHECK(pty.open(error));
    if (pty.master < 0) {
        return;
    }

    io::Df1SerialConfig config;
    config.device = pty.slave;
    config.timeout_ms = 300;
    io::Df1SerialLink link(config);
    CHECK(link.open(error));

    const std::vector<u8> frame = reply_frame(1, io::Df1Checksum::Bcc);
    CHECK(!frame.empty());
    std::vector<u8> peer = {io::kDf1Dle, 0x06};
    peer.insert(peer.end(), frame.begin(), frame.end());
    CHECK(write_all(pty.master, peer));

    std::vector<u8> reply;
    CHECK(link.exchange(read_request_frame(io::Df1Checksum::Bcc), reply, error));
    CHECK(reply == frame);
}

TEST(df1_serial_link_acknowledges_a_valid_reply) {
    PtyPair pty;
    std::string error;
    CHECK(pty.open(error));
    if (pty.master < 0) {
        return;
    }

    io::Df1SerialConfig config;
    config.device = pty.slave;
    config.timeout_ms = 300;
    io::Df1SerialLink link(config);
    CHECK(link.open(error));

    CHECK(write_all(pty.master, reply_frame(1, io::Df1Checksum::Bcc)));

    std::vector<u8> reply;
    CHECK(link.exchange(read_request_frame(io::Df1Checksum::Bcc), reply, error));

    const std::vector<u8> sent = drain_host_bytes(pty.master);
    CHECK(sent.size() >= 2);
    if (sent.size() >= 2) {
        CHECK(sent[sent.size() - 2] == io::kDf1Dle);
        CHECK(sent[sent.size() - 1] == 0x06);
    }
}

TEST(df1_serial_link_naks_a_corrupt_reply) {
    PtyPair pty;
    std::string error;
    CHECK(pty.open(error));
    if (pty.master < 0) {
        return;
    }

    io::Df1SerialConfig config;
    config.device = pty.slave;
    config.timeout_ms = 300;
    io::Df1SerialLink link(config);
    CHECK(link.open(error));

    std::vector<u8> frame = reply_frame(1, io::Df1Checksum::Bcc);
    CHECK(!frame.empty());
    if (!frame.empty()) {
        frame.back() = static_cast<u8>(frame.back() ^ 0xFFu);
    }
    CHECK(write_all(pty.master, frame));

    std::vector<u8> reply;
    CHECK(!link.exchange(read_request_frame(io::Df1Checksum::Bcc), reply, error));
    CHECK(error.find("checksum") != std::string::npos);

    const std::vector<u8> sent = drain_host_bytes(pty.master);
    CHECK(sent.size() >= 2);
    if (sent.size() >= 2) {
        CHECK(sent[sent.size() - 2] == io::kDf1Dle);
        CHECK(sent[sent.size() - 1] == 0x15);
    }
}

#endif
