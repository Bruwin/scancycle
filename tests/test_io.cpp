#include "harness.hpp"

#include "io/commissioning.hpp"
#include "io/force_table.hpp"
#include "io/modbus.hpp"

using namespace sc;

TEST(modbus_builds_a_read_holding_registers_request) {
    const std::vector<u8> frame = io::build_read_holding_registers(0x0102, 1, 0x0004, 2);

    CHECK(frame.size() == 12);
    CHECK(frame[0] == 0x01);
    CHECK(frame[1] == 0x02);
    CHECK(frame[2] == 0x00);
    CHECK(frame[3] == 0x00);
    CHECK(frame[4] == 0x00);
    CHECK(frame[5] == 0x06);
    CHECK(frame[6] == 0x01);
    CHECK(frame[7] == io::kFuncReadHoldingRegisters);
    CHECK(frame[10] == 0x00);
    CHECK(frame[11] == 0x02);
}

TEST(modbus_parses_a_read_response) {
    // transaction 1, protocol 0, length 7, unit 1, function 3, 4 data bytes.
    const std::vector<u8> frame = {0, 1, 0, 0, 0, 7, 1, 3, 4, 0, 10, 0, 11};

    io::ModbusResponse response;
    std::string error;
    CHECK(io::parse_read_response(frame, response, error));
    CHECK(error.empty());
    CHECK(response.transaction == 1);
    CHECK(response.unit == 1);
    CHECK(response.registers.size() == 2);
    CHECK(response.registers[0] == 10);
    CHECK(response.registers[1] == 11);
}

TEST(modbus_rejects_a_malformed_response) {
    io::ModbusResponse response;
    std::string error;

    const std::vector<u8> short_frame = {0, 1, 0};
    CHECK(!io::parse_read_response(short_frame, response, error));
    CHECK(!error.empty());

    // Length field claims seven bytes but only four follow.
    const std::vector<u8> bad_length = {0, 1, 0, 0, 0, 7, 1, 3, 4, 0};
    CHECK(!io::parse_read_response(bad_length, response, error));

    // Exception response: function 3 with the high bit set.
    const std::vector<u8> exception = {0, 1, 0, 0, 0, 3, 1, 0x83, 2};
    CHECK(!io::parse_read_response(exception, response, error));
    CHECK(error.find("exception") != std::string::npos);
}

TEST(modbus_parses_a_write_response) {
    const std::vector<u8> frame = {0, 2, 0, 0, 0, 6, 1, 6, 0, 4, 0, 99};

    u16 address = 0;
    u16 value = 0;
    std::string error;
    CHECK(io::parse_write_response(frame, address, value, error));
    CHECK(address == 4);
    CHECK(value == 99);
}

TEST(commissioning_denies_writes_by_default) {
    io::CommissioningPolicy policy;
    CHECK(!policy.can_write("DB1.Motor", 0));
    CHECK(policy.writes_denied() == 1);
    CHECK(policy.writes_permitted() == 0);
}

TEST(commissioning_requires_session_and_allowlist) {
    io::CommissioningPolicy policy;
    policy.allow_address("DB1.Motor");
    CHECK(policy.is_allowed("DB1.Motor"));
    CHECK(!policy.is_allowed("DB1.Valve"));

    // Allowlisted but no session open.
    CHECK(!policy.can_write("DB1.Motor", 0));

    CHECK(policy.begin_session(0, 1000));
    CHECK(policy.session_active(500));
    CHECK(policy.can_write("DB1.Motor", 0));

    // Outside the allowlist, during an open session.
    CHECK(!policy.can_write("DB1.Valve", 200));

    // Session expired.
    CHECK(!policy.session_active(1001));
    CHECK(!policy.can_write("DB1.Motor", 1001));
}

TEST(commissioning_rate_limits_writes) {
    io::CommissioningPolicy policy;
    policy.allow_address("DB1.Motor");
    policy.set_min_write_interval_ms(100);
    policy.begin_session(0, 10'000);

    CHECK(policy.can_write("DB1.Motor", 0));
    CHECK(!policy.can_write("DB1.Motor", 50));
    CHECK(policy.can_write("DB1.Motor", 100));
    CHECK(policy.writes_permitted() == 2);
    CHECK(policy.writes_denied() == 1);
}

TEST(commissioning_ends_sessions) {
    io::CommissioningPolicy policy;
    policy.allow_address("DB1.Motor");
    policy.begin_session(0, 10'000);
    CHECK(policy.session_active(0));

    policy.end_session();
    CHECK(!policy.session_active(0));
    CHECK(!policy.can_write("DB1.Motor", 0));
}

TEST(force_table_replaces_and_clears_by_name) {
    io::ForceTable forces;
    CHECK(forces.empty());

    forces.set("motor", true);
    forces.set("eye", false);
    CHECK(forces.size() == 2);

    bool value = false;
    CHECK(forces.find("motor", value));
    CHECK(value);

    // Setting an existing name replaces it instead of adding a second entry.
    forces.set("motor", false);
    CHECK(forces.size() == 2);
    CHECK(forces.find("motor", value));
    CHECK(!value);

    CHECK(forces.forced("motor"));
    CHECK(!forces.forced("missing"));

    CHECK(forces.clear("motor"));
    CHECK(!forces.clear("motor"));
    CHECK(forces.size() == 1);

    forces.clear_all();
    CHECK(forces.empty());
}
