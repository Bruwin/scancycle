#include "harness.hpp"

#include "io/sim_driver.hpp"
#include "io/tag_database.hpp"
#include "plc/compiler.hpp"
#include "plc/parser.hpp"
#include "sim/plant.hpp"

using namespace sc;

TEST(plant_motor_ramps_to_full_speed) {
    sim::Plant plant;
    const u32 motor = plant.add(sim::DeviceKind::Motor, "motor");
    plant.set_output(motor, true);

    for (int tick = 0; tick < 100; ++tick) {
        plant.tick(0.01);
    }

    CHECK(plant.device(motor).speed > 0.99);
    CHECK(plant.input(motor));
}

TEST(plant_motor_stops_when_the_command_drops) {
    sim::Plant plant;
    const u32 motor = plant.add(sim::DeviceKind::Motor, "motor");
    plant.set_output(motor, true);
    for (int tick = 0; tick < 100; ++tick) {
        plant.tick(0.01);
    }

    plant.set_output(motor, false);
    for (int tick = 0; tick < 100; ++tick) {
        plant.tick(0.01);
    }

    CHECK(plant.device(motor).speed < 0.01);
    CHECK(!plant.input(motor));
}

TEST(plant_belt_counts_boxes) {
    sim::Plant plant;
    const u32 belt = plant.add(sim::DeviceKind::Belt, "belt");
    plant.device(belt).ramp_seconds = 0.0;
    plant.set_output(belt, true);

    for (int tick = 0; tick < 300; ++tick) {
        plant.tick(0.01);
    }

    CHECK(plant.device(belt).box_count >= 2);
}

TEST(plant_ignores_non_positive_ticks) {
    sim::Plant plant;
    const u32 belt = plant.add(sim::DeviceKind::Belt, "belt");
    plant.device(belt).ramp_seconds = 0.0;
    plant.set_output(belt, true);

    plant.tick(0.0);
    plant.tick(-1.0);

    CHECK(plant.device(belt).distance == 0.0);
    CHECK(plant.device(belt).speed == 0.0);
}

TEST(plant_is_deterministic) {
    sim::Plant a;
    sim::Plant b;
    for (sim::Plant* plant : {&a, &b}) {
        plant->add(sim::DeviceKind::Belt, "belt");
        plant->add(sim::DeviceKind::PhotoEye, "eye");
        plant->set_output(0, true);
        plant->set_blocked(1, true);
    }

    for (int tick = 0; tick < 500; ++tick) {
        a.tick(0.016);
        b.tick(0.016);
    }

    CHECK(a.device(0).box_count == b.device(0).box_count);
    CHECK(a.device(0).speed == b.device(0).speed);
    CHECK(a.device(1).input == b.device(1).input);
}

TEST(tag_database_round_trip) {
    sim::Plant plant;
    const u32 motor = plant.add(sim::DeviceKind::Motor, "motor");

    io::TagDatabase tags;
    const u32 out = tags.bind("motor_run", io::TagDirection::Output, motor);
    const u32 in = tags.bind("motor_fb", io::TagDirection::Input, motor);

    CHECK(tags.size() == 2);
    CHECK(tags.find("motor_run") == static_cast<i32>(out));
    CHECK(tags.find("missing") == -1);

    CHECK(tags.write_bool(out, true, plant));
    CHECK(plant.device(motor).output);

    // Direction is enforced: an output tag cannot be read as an input.
    bool value = true;
    CHECK(!tags.read_bool(out, plant, value));
    CHECK(tags.read_bool(in, plant, value));
}

TEST(sim_driver_closes_the_loop) {
    sim::Plant plant;
    const u32 motor = plant.add(sim::DeviceKind::Motor, "motor");

    io::TagDatabase tags;
    tags.bind("motor", io::TagDirection::Output, motor);
    tags.bind("motor_fb", io::TagDirection::Input, motor);

    const char* source = R"st(PROGRAM Drive
VAR
    start_btn : BOOL;
    motor     : BOOL;
    motor_fb  : BOOL;
END_VAR
    motor := start_btn;
END_PROGRAM
)st";

    plc::ParseResult parsed = plc::parse_program(source);
    CHECK(static_cast<bool>(parsed.program));
    if (!parsed.program) {
        return;
    }

    plc::CompileResult compiled = plc::compile(*parsed.program);
    CHECK(compiled.ok);
    if (!compiled.ok) {
        return;
    }

    plc::CompiledProgram program = std::move(compiled.program);
    plc::Vm vm(program);

    io::SimDriver driver(plant, tags, vm);
    CHECK(driver.connect());
    CHECK(vm.set("start_btn", plc::Value::boolean(true)));

    CHECK(driver.run_scan(0));
    CHECK(plant.device(motor).output);
    CHECK(driver.scans() == 1);

    for (int tick = 0; tick < 100; ++tick) {
        plant.tick(0.01);
    }
    CHECK(driver.run_scan(100));
    CHECK(vm.get("motor_fb").as_bool());
}

TEST(sim_driver_rejects_a_program_without_the_tag) {
    sim::Plant plant;
    const u32 motor = plant.add(sim::DeviceKind::Motor, "motor");

    io::TagDatabase tags;
    tags.bind("undeclared_tag", io::TagDirection::Input, motor);

    const char* source = R"st(PROGRAM Drive
VAR
    start_btn : BOOL;
END_VAR
    start_btn := TRUE;
END_PROGRAM
)st";

    plc::ParseResult parsed = plc::parse_program(source);
    CHECK(static_cast<bool>(parsed.program));
    if (!parsed.program) {
        return;
    }
    plc::CompileResult compiled = plc::compile(*parsed.program);
    CHECK(compiled.ok);
    if (!compiled.ok) {
        return;
    }

    plc::CompiledProgram program = std::move(compiled.program);
    plc::Vm vm(program);
    io::SimDriver driver(plant, tags, vm);
    CHECK(driver.connect());
    CHECK(!driver.run_scan(0));
    CHECK(!driver.last_error().empty());
}
