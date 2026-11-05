#include "harness.hpp"

#include "io/commissioning.hpp"
#include "io/modbus.hpp"
#include "io/modbus_slave.hpp"
#include "io/modbus_tcp.hpp"
#include "plc/compiler.hpp"
#include "plc/parser.hpp"

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
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

io::ModbusTcpConfig make_config(const io::ModbusTcpSlave& slave, io::CommissioningPolicy* policy) {
    io::ModbusTcpConfig config;
    config.host = "127.0.0.1";
    config.port = slave.port();
    config.unit = 1;
    config.timeout_ms = 1000;
    config.tags = {
        io::ModbusTag{"start", 0, io::ModbusTagDirection::Input},
        io::ModbusTag{"run", 1, io::ModbusTagDirection::Output},
    };
    config.commissioning = policy;
    return config;
}

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

}  // namespace

TEST(modbus_builds_a_read_response) {
    const std::vector<u8> frame = io::build_read_response(7, 1, {10, 11});
    CHECK(frame.size() == 13);
    CHECK(frame[0] == 0);
    CHECK(frame[1] == 7);
    CHECK(frame[4] == 0);
    CHECK(frame[5] == 7);
    CHECK(frame[7] == io::kFuncReadHoldingRegisters);
    CHECK(frame[8] == 4);
    CHECK(frame[9] == 0);
    CHECK(frame[10] == 10);
    CHECK(frame[11] == 0);
    CHECK(frame[12] == 11);
}

TEST(modbus_builds_an_exception_response) {
    const std::vector<u8> frame = io::build_exception_response(7, 1, 3, 2);
    CHECK(frame.size() == 9);
    CHECK(frame[4] == 0);
    CHECK(frame[5] == 3);
    CHECK(frame[7] == 0x83);
    CHECK(frame[8] == 2);
}

TEST(modbus_driver_reads_inputs_and_writes_changed_outputs) {
    io::ModbusTcpSlave slave;
    CHECK(slave.listen(0));
    if (slave.port() == 0) {
        return;
    }
    slave.set_register(0, 1);

    std::atomic<bool> served{false};
    std::thread server([&] { served.store(slave.serve_one()); });

    plc::CompiledProgram program;
    CHECK(compile_wire(program));
    if (program.code.empty()) {
        slave.stop();
        server.join();
        return;
    }
    plc::Vm vm(program);

    io::CommissioningPolicy policy;
    policy.allow_address("run");
    policy.set_min_write_interval_ms(0);
    CHECK(policy.begin_session(0, 10000));

    io::ModbusTcpConfig config = make_config(slave, &policy);
    io::ModbusTcpDriver driver(std::move(config), vm);
    CHECK(driver.connect());
    if (!driver.connected()) {
        slave.stop();
        server.join();
        return;
    }

    CHECK(driver.run_scan(0));
    CHECK(vm.get("run").as_bool());
    CHECK(slave.register_value(1) == 1);
    CHECK(policy.writes_permitted() == 1);

    // The unchanged output is not written again.
    CHECK(driver.run_scan(10));
    CHECK(policy.writes_permitted() == 1);

    slave.set_register(0, 0);
    CHECK(driver.run_scan(20));
    CHECK(!vm.get("run").as_bool());
    CHECK(slave.register_value(1) == 0);
    CHECK(policy.writes_permitted() == 2);

    driver.disconnect();
    server.join();
    CHECK(served.load());
}

TEST(modbus_driver_fails_closed_without_commissioning) {
    io::ModbusTcpSlave slave;
    CHECK(slave.listen(0));
    if (slave.port() == 0) {
        return;
    }

    std::atomic<bool> served{false};
    std::thread server([&] { served.store(slave.serve_one()); });

    plc::CompiledProgram program;
    CHECK(compile_wire(program));
    if (program.code.empty()) {
        slave.stop();
        server.join();
        return;
    }
    plc::Vm vm(program);

    io::ModbusTcpConfig config = make_config(slave, nullptr);
    io::ModbusTcpDriver driver(std::move(config), vm);
    CHECK(driver.connect());
    if (!driver.connected()) {
        slave.stop();
        server.join();
        return;
    }

    CHECK(!driver.run_scan(0));
    CHECK(driver.last_error().find("commissioning") != std::string::npos);
    CHECK(slave.register_value(1) == 0);

    driver.disconnect();
    server.join();
    CHECK(served.load());
}

TEST(modbus_slave_stops_while_waiting_for_a_client) {
    io::ModbusTcpSlave slave;
    CHECK(slave.listen(0));
    if (slave.port() == 0) {
        return;
    }

    std::atomic<bool> served{true};
    std::thread server([&] { served.store(slave.serve_one()); });
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    slave.stop();
    server.join();
    CHECK(!served.load());
}

TEST(modbus_driver_read_only_tags_need_no_commissioning) {
    io::ModbusTcpSlave slave;
    CHECK(slave.listen(0));
    if (slave.port() == 0) {
        return;
    }
    slave.set_register(0, 1);

    std::atomic<bool> served{false};
    std::thread server([&] { served.store(slave.serve_one()); });

    plc::CompiledProgram program;
    CHECK(compile_wire(program));
    if (program.code.empty()) {
        slave.stop();
        server.join();
        return;
    }
    plc::Vm vm(program);

    io::ModbusTcpConfig config;
    config.host = "127.0.0.1";
    config.port = slave.port();
    config.tags = {io::ModbusTag{"start", 0, io::ModbusTagDirection::Input}};
    io::ModbusTcpDriver driver(std::move(config), vm);
    CHECK(driver.connect());
    if (!driver.connected()) {
        slave.stop();
        server.join();
        return;
    }

    CHECK(driver.run_scan(0));
    CHECK(vm.get("start").as_bool());
    CHECK(driver.scans() == 1);

    driver.disconnect();
    server.join();
    CHECK(served.load());
}
