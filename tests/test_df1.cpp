#include "harness.hpp"

#include "io/commissioning.hpp"
#include "io/df1.hpp"
#include "io/df1_driver.hpp"
#include "io/df1_pccc.hpp"
#include "io/df1_slave.hpp"
#include "plc/compiler.hpp"
#include "plc/parser.hpp"

#include <string>
#include <utility>
#include <vector>

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

std::vector<u8> frame_for(const io::Df1Header& header, const std::vector<u8>& payload,
                          io::Df1Checksum checksum = io::Df1Checksum::Bcc) {
    std::vector<u8> body = io::df1_body(header);
    body.insert(body.end(), payload.begin(), payload.end());
    std::vector<u8> frame;
    std::string error;
    CHECK(io::df1_encode(body, checksum, frame, error));
    return frame;
}

}  // namespace

TEST(df1_bcc_complements_the_body_sum) {
    const std::vector<u8> body = {0x01, 0x02, 0x10, 0xFF};
    const u8 bcc = io::df1_bcc(body);

    u32 sum = bcc;
    for (const u8 byte : body) {
        sum += byte;
    }
    CHECK((sum & 0xFFu) == 0);
    CHECK(bcc == 0xEE);
}

TEST(df1_crc16_matches_the_arc_check_value) {
    const std::string text = "123456789";
    const std::vector<u8> bytes(text.begin(), text.end());
    CHECK(io::df1_crc16(bytes) == 0xBB3D);
}

TEST(df1_frame_escapes_dle_bytes) {
    const std::vector<u8> body = {0x10, 0x02, 0x03, 0x10};
    std::vector<u8> frame;
    std::string error;
    CHECK(io::df1_encode(body, io::Df1Checksum::Bcc, frame, error));
    CHECK(error.empty());

    // DLE STX, the four body bytes with two DLEs doubled, DLE ETX, one BCC.
    CHECK(frame.size() == 2 + 6 + 2 + 1);
    CHECK(frame[0] == io::kDf1Dle);
    CHECK(frame[1] == io::kDf1Stx);
    CHECK(frame[2] == 0x10);
    CHECK(frame[3] == 0x10);

    std::vector<u8> decoded;
    std::size_t consumed = 0;
    CHECK(io::df1_decode(frame, io::Df1Checksum::Bcc, decoded, consumed, error));
    CHECK(consumed == frame.size());
    CHECK(decoded == body);
}

TEST(df1_decode_rejects_a_corrupt_checksum) {
    const std::vector<u8> body = {0x00, 0x01, 0x0F, 0x00, 0x01, 0x00};
    std::vector<u8> frame;
    std::string error;
    CHECK(io::df1_encode(body, io::Df1Checksum::Bcc, frame, error));

    frame.back() ^= 0xFF;
    std::vector<u8> decoded;
    std::size_t consumed = 0;
    CHECK(!io::df1_decode(frame, io::Df1Checksum::Bcc, decoded, consumed, error));
    CHECK(error.find("checksum") != std::string::npos);
    CHECK(consumed == frame.size());
}

TEST(df1_decode_skips_noise_and_waits_for_a_complete_frame) {
    const std::vector<u8> body = {0x00, 0x01, 0x0F, 0x00, 0x01, 0x00};
    std::vector<u8> frame;
    std::string error;
    CHECK(io::df1_encode(body, io::Df1Checksum::Crc16, frame, error));

    std::vector<u8> stream = {0xAA, 0x55};
    stream.insert(stream.end(), frame.begin(), frame.end());

    std::vector<u8> decoded;
    std::size_t consumed = 0;
    CHECK(io::df1_decode(stream, io::Df1Checksum::Crc16, decoded, consumed, error));
    CHECK(decoded == body);
    CHECK(consumed == stream.size());

    std::vector<u8> partial(frame.begin(), frame.end() - 1);
    consumed = 99;
    CHECK(!io::df1_decode(partial, io::Df1Checksum::Crc16, decoded, consumed, error));
    CHECK(consumed == 0);
    CHECK(error.find("incomplete") != std::string::npos);
}

TEST(pccc_read_reply_parses_little_endian_words) {
    io::Df1Header header;
    header.destination = 1;
    header.source = 0;
    header.command = io::kDf1CmdReadReply;
    header.transaction = 9;

    const io::PcccAddress address{7, io::kPcccFileTypeInteger, 3, 0};
    const std::vector<u16> words = {0x1234, 0x00FF};
    const std::vector<u8> payload = io::build_pccc_read_reply(address, words);
    CHECK(payload.size() == 7 + 4);
    CHECK(payload[7] == 0x34);

    std::vector<u8> body = io::df1_body(header);
    body.insert(body.end(), payload.begin(), payload.end());

    io::Df1Header expected;
    expected.transaction = 9;
    io::PcccReply reply;
    std::string error;
    CHECK(io::parse_pccc_read_reply(body, expected, address, reply, error));
    CHECK(error.empty());
    CHECK(reply.words == words);
    CHECK(reply.address == address);

    // A reply to a different transaction must not be accepted.
    expected.transaction = 10;
    CHECK(!io::parse_pccc_read_reply(body, expected, address, reply, error));
    CHECK(error.find("transaction") != std::string::npos);
}

TEST(df1_slave_answers_a_typed_read_and_write) {
    io::Df1Slave slave;
    slave.add_word_file(7, io::kPcccFileTypeInteger, 4);
    CHECK(slave.set_word(7, 0, 0xBEEF));

    const io::PcccAddress address{7, io::kPcccFileTypeInteger, 0, 0};
    io::Df1Header header;
    header.destination = 0;
    header.source = 1;
    header.command = io::kDf1CmdProtectedRead;
    header.transaction = 1;

    std::vector<u8> reply;
    std::string error;
    CHECK(slave.exchange(frame_for(header, io::build_pccc_read_request(address, 2)), reply,
                         error));

    std::vector<u8> body;
    std::size_t consumed = 0;
    CHECK(io::df1_decode(reply, io::Df1Checksum::Bcc, body, consumed, error));
    io::PcccReply parsed;
    CHECK(io::parse_pccc_read_reply(body, header, address, parsed, error));
    CHECK(parsed.words.size() == 1);
    CHECK(parsed.words[0] == 0xBEEF);
    CHECK(parsed.header.destination == 1);
    CHECK(parsed.header.source == 0);

    header.command = io::kDf1CmdProtectedWrite;
    header.transaction = 2;
    const std::vector<u16> words = {0x1234};
    reply.clear();
    CHECK(slave.exchange(frame_for(header, io::build_pccc_write_request(address, words)),
                         reply, error));
    body.clear();
    CHECK(io::df1_decode(reply, io::Df1Checksum::Bcc, body, consumed, error));
    CHECK(io::parse_pccc_write_reply(body, header, address, error));
    CHECK(slave.word(7, 0) == 0x1234);
    CHECK(slave.writes() == 1);
    CHECK(slave.requests() == 2);
}

TEST(df1_slave_rejects_an_element_out_of_range) {
    io::Df1Slave slave;
    slave.add_word_file(7, io::kPcccFileTypeInteger, 2);

    const io::PcccAddress address{7, io::kPcccFileTypeInteger, 2, 0};
    io::Df1Header header;
    header.destination = 0;
    header.source = 1;
    header.command = io::kDf1CmdProtectedRead;
    header.transaction = 3;

    std::vector<u8> reply;
    std::string error;
    CHECK(slave.exchange(frame_for(header, io::build_pccc_read_request(address, 2)), reply,
                         error));

    std::vector<u8> body;
    std::size_t consumed = 0;
    CHECK(io::df1_decode(reply, io::Df1Checksum::Bcc, body, consumed, error));
    io::PcccReply parsed;
    CHECK(!io::parse_pccc_read_reply(body, header, address, parsed, error));
    CHECK(error.find("status 0xF0") != std::string::npos);
}

TEST(df1_driver_latches_inputs_and_writes_changed_outputs) {
    io::Df1Slave slave;
    slave.add_word_file(7, io::kPcccFileTypeInteger, 8);
    CHECK(slave.set_bit(7, 0, 0, true));

    plc::CompiledProgram program;
    CHECK(compile_wire(program));
    if (program.code.empty()) {
        return;
    }
    plc::Vm vm(program);

    io::CommissioningPolicy policy;
    policy.allow_address("run");
    policy.set_min_write_interval_ms(0);
    CHECK(policy.begin_session(0, 10'000));

    io::Df1Config config;
    config.tags = {
        tag("start", 0, 0, io::Df1Direction::Input),
        tag("run", 1, 0, io::Df1Direction::Output),
    };
    config.commissioning = &policy;

    io::Df1Driver driver(config, vm, slave);
    CHECK(driver.connect());
    CHECK(driver.connected());

    CHECK(driver.run_scan(0));
    CHECK(vm.get("run").as_bool());
    CHECK(slave.bit(7, 1, 0));
    CHECK(slave.writes() == 1);
    CHECK(policy.writes_permitted() == 1);

    // Input read, output read, output write.
    const i64 first_scan = driver.exchanges();
    CHECK(first_scan == 3);

    // An unchanged output costs one input read and no write.
    CHECK(driver.run_scan(10));
    CHECK(slave.writes() == 1);
    CHECK(driver.exchanges() == first_scan + 1);

    CHECK(slave.set_bit(7, 0, 0, false));
    CHECK(driver.run_scan(20));
    CHECK(!vm.get("run").as_bool());
    CHECK(!slave.bit(7, 1, 0));
    CHECK(slave.writes() == 2);
    CHECK(driver.scans() == 3);

    driver.disconnect();
    CHECK(!driver.connected());
}

TEST(df1_driver_fails_closed_without_commissioning) {
    io::Df1Slave slave;
    slave.add_word_file(7, io::kPcccFileTypeInteger, 8);
    CHECK(slave.set_bit(7, 0, 0, true));

    plc::CompiledProgram program;
    CHECK(compile_wire(program));
    if (program.code.empty()) {
        return;
    }
    plc::Vm vm(program);

    io::Df1Config config;
    config.tags = {
        tag("start", 0, 0, io::Df1Direction::Input),
        tag("run", 1, 0, io::Df1Direction::Output),
    };

    io::Df1Driver driver(config, vm, slave);
    CHECK(driver.connect());
    CHECK(!driver.run_scan(0));
    CHECK(driver.last_error().find("commissioning") != std::string::npos);
    CHECK(slave.writes() == 0);
    CHECK(!slave.bit(7, 1, 0));
}

TEST(df1_driver_preserves_the_other_bits_of_the_word) {
    io::Df1Slave slave;
    slave.add_word_file(7, io::kPcccFileTypeInteger, 8);
    CHECK(slave.set_bit(7, 0, 0, true));
    CHECK(slave.set_word(7, 1, 0x0002));

    plc::CompiledProgram program;
    CHECK(compile_wire(program));
    if (program.code.empty()) {
        return;
    }
    plc::Vm vm(program);

    io::CommissioningPolicy policy;
    policy.allow_address("run");
    policy.set_min_write_interval_ms(0);
    CHECK(policy.begin_session(0, 10'000));

    io::Df1Config config;
    config.tags = {
        tag("start", 0, 0, io::Df1Direction::Input),
        tag("run", 1, 0, io::Df1Direction::Output),
    };
    config.commissioning = &policy;

    io::Df1Driver driver(config, vm, slave);
    CHECK(driver.connect());

    CHECK(driver.run_scan(0));
    CHECK(slave.word(7, 1) == 0x0003);

    CHECK(slave.set_bit(7, 0, 0, false));
    CHECK(driver.run_scan(10));
    CHECK(slave.word(7, 1) == 0x0002);
}

TEST(df1_driver_reports_a_silent_controller) {
    io::Df1Slave slave;
    slave.add_word_file(7, io::kPcccFileTypeInteger, 8);
    slave.set_silent(true);

    plc::CompiledProgram program;
    CHECK(compile_wire(program));
    if (program.code.empty()) {
        return;
    }
    plc::Vm vm(program);

    io::Df1Config config;
    config.tags = {tag("start", 0, 0, io::Df1Direction::Input)};

    io::Df1Driver driver(config, vm, slave);
    CHECK(driver.connect());
    CHECK(!driver.run_scan(0));
    CHECK(driver.last_error().find("answer") != std::string::npos);
}

TEST(df1_driver_speaks_crc16_links) {
    io::Df1Slave slave;
    slave.add_word_file(7, io::kPcccFileTypeInteger, 8);
    slave.set_checksum(io::Df1Checksum::Crc16);
    CHECK(slave.set_bit(7, 0, 5, true));

    plc::CompiledProgram program;
    CHECK(compile_wire(program));
    if (program.code.empty()) {
        return;
    }
    plc::Vm vm(program);

    io::Df1Config config;
    config.checksum = io::Df1Checksum::Crc16;
    config.tags = {tag("start", 0, 5, io::Df1Direction::Input)};

    io::Df1Driver driver(config, vm, slave);
    CHECK(driver.connect());
    CHECK(driver.run_scan(0));
    CHECK(vm.get("start").as_bool());
}

TEST(df1_driver_reports_a_controller_status_error) {
    io::Df1Slave slave;  // No data file registered.

    plc::CompiledProgram program;
    CHECK(compile_wire(program));
    if (program.code.empty()) {
        return;
    }
    plc::Vm vm(program);

    io::Df1Config config;
    config.tags = {tag("start", 0, 0, io::Df1Direction::Input)};

    io::Df1Driver driver(config, vm, slave);
    CHECK(driver.connect());
    CHECK(!driver.run_scan(0));
    CHECK(driver.last_error().find("status 0xF0") != std::string::npos);
}

TEST(df1_driver_rejects_an_unsupported_file_type_at_connect) {
    io::Df1Slave slave;
    slave.add_word_file(7, io::kPcccFileTypeInteger, 8);

    plc::CompiledProgram program;
    CHECK(compile_wire(program));
    if (program.code.empty()) {
        return;
    }
    plc::Vm vm(program);

    io::Df1Config config;
    io::Df1Tag timer = tag("start", 0, 0, io::Df1Direction::Input);
    timer.file_type = 0x86;  // Timer file: not part of this slice.
    config.tags = {timer};

    io::Df1Driver driver(config, vm, slave);
    CHECK(!driver.connect());
    CHECK(driver.last_error().find("file type") != std::string::npos);
}
