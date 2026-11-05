#include "harness.hpp"

#include "io/sim_driver.hpp"
#include "io/tag_database.hpp"
#include "plc/compiler.hpp"
#include "plc/parser.hpp"
#include "sim/plant.hpp"

using namespace sc;

namespace {

// A belt that reaches full speed on the first tick, so tests can reason in
// metres rather than in ramp curves.
u32 make_instant_belt(sim::Plant& plant, const char* name, f64 length) {
    const u32 belt = plant.add(sim::DeviceKind::Belt, name);
    plant.device(belt).ramp_seconds = 0.0;
    plant.device(belt).length = length;
    return belt;
}

}  // namespace

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

TEST(belt_delivers_a_box_past_its_end) {
    sim::Plant plant;
    const u32 belt = make_instant_belt(plant, "belt", 1.0);
    plant.set_output(belt, true);
    plant.spawn_box(belt);

    CHECK(plant.box_count() == 1);
    for (int tick = 0; tick < 200; ++tick) {
        plant.tick(0.01);
    }

    CHECK(plant.box_count() == 0);
    CHECK(plant.device(belt).box_count == 1);
}

TEST(boxes_transfer_to_the_downstream_belt) {
    sim::Plant plant;
    const u32 upstream = make_instant_belt(plant, "upstream", 1.0);
    const u32 downstream = make_instant_belt(plant, "downstream", 2.0);
    plant.device(upstream).next = downstream;
    plant.set_output(upstream, true);
    plant.set_output(downstream, true);

    plant.spawn_box(upstream);
    for (int tick = 0; tick < 150; ++tick) {
        plant.tick(0.01);
    }

    CHECK(plant.boxes_on(upstream) == 0);
    CHECK(plant.boxes_on(downstream) == 1);
    CHECK(plant.device(upstream).box_count == 1);
    CHECK(plant.device(upstream).jammed == false);
}

TEST(photo_eye_senses_a_box_in_its_window) {
    sim::Plant plant;
    const u32 belt = make_instant_belt(plant, "belt", 4.0);
    const u32 eye = plant.add(sim::DeviceKind::PhotoEye, "eye");
    plant.device(eye).belt = belt;
    plant.device(eye).at = 1.0;
    plant.device(eye).window = 0.2;

    plant.set_output(belt, true);
    plant.spawn_box(belt);
    CHECK(!plant.input(eye));

    bool ever_sensed = false;
    for (int tick = 0; tick < 200; ++tick) {
        plant.tick(0.01);
        ever_sensed = ever_sensed || plant.input(eye);
    }

    CHECK(ever_sensed);
    // The box has travelled well past the sensing band by now.
    CHECK(!plant.input(eye));
}

TEST(photo_eye_honours_a_manual_occlusion_override) {
    sim::Plant plant;
    const u32 belt = make_instant_belt(plant, "belt", 4.0);
    const u32 eye = plant.add(sim::DeviceKind::PhotoEye, "eye");
    plant.device(eye).belt = belt;
    plant.device(eye).at = 1.0;

    plant.set_blocked(eye, true);
    plant.tick(0.01);
    CHECK(plant.input(eye));

    plant.set_blocked(eye, false);
    plant.tick(0.01);
    CHECK(!plant.input(eye));
}

TEST(belt_jams_when_the_downstream_belt_has_no_room) {
    sim::Plant plant;
    const u32 upstream = make_instant_belt(plant, "upstream", 1.0);
    const u32 downstream = make_instant_belt(plant, "downstream", 2.0);
    plant.device(upstream).next = downstream;

    // A box already sitting at the downstream infeed blocks the hand-off.
    plant.spawn_box(downstream);
    plant.set_output(upstream, true);
    plant.spawn_box(upstream);

    for (int tick = 0; tick < 200; ++tick) {
        plant.tick(0.01);
    }

    CHECK(plant.device(upstream).jammed);
    CHECK(plant.boxes_on(upstream) == 1);
    CHECK(plant.device(upstream).box_count == 0);
}

TEST(spawner_does_not_stack_boxes_on_a_stopped_belt) {
    sim::Plant plant;
    const u32 belt = make_instant_belt(plant, "belt", 4.0);
    plant.device(belt).spawn_period = 0.5;

    for (int tick = 0; tick < 500; ++tick) {
        plant.tick(0.01);
    }

    // The infeed is never clear while a box sits at the head of a stopped belt.
    CHECK(plant.boxes_on(belt) <= 1);
}

TEST(plant_is_deterministic) {
    sim::Plant a;
    sim::Plant b;
    for (sim::Plant* plant : {&a, &b}) {
        const u32 belt = make_instant_belt(*plant, "belt", 3.0);
        const u32 eye = plant->add(sim::DeviceKind::PhotoEye, "eye");
        plant->device(eye).belt = belt;
        plant->device(eye).at = 1.5;
        plant->device(belt).spawn_period = 0.7;
        plant->set_output(belt, true);
    }

    for (int tick = 0; tick < 500; ++tick) {
        a.tick(0.016);
        b.tick(0.016);
    }

    CHECK(a.box_count() == b.box_count());
    CHECK(a.device(0).box_count == b.device(0).box_count);
    for (u32 i = 0; i < a.box_count() && i < b.box_count(); ++i) {
        CHECK(a.box(i).position == b.box(i).position);
        CHECK(a.box(i).belt == b.box(i).belt);
    }
}

TEST(plant_ignores_non_positive_ticks) {
    sim::Plant plant;
    const u32 belt = make_instant_belt(plant, "belt", 4.0);
    plant.set_output(belt, true);

    plant.tick(0.0);
    plant.tick(-1.0);

    CHECK(plant.device(belt).distance == 0.0);
    CHECK(plant.device(belt).speed == 0.0);
}

TEST(plant_reset_clears_boxes_and_counters) {
    sim::Plant plant;
    const u32 belt = make_instant_belt(plant, "belt", 1.0);
    plant.set_output(belt, true);
    plant.spawn_box(belt);

    for (int tick = 0; tick < 200; ++tick) {
        plant.tick(0.01);
    }
    CHECK(plant.device(belt).box_count == 1);

    plant.reset();
    CHECK(plant.box_count() == 0);
    CHECK(plant.device(belt).box_count == 0);
    CHECK(plant.device(belt).speed == 0.0);
}

TEST(vertical_slice_plant_is_wired) {
    const sim::Plant plant = sim::make_vertical_slice_plant();

    CHECK(plant.count() == 5);
    CHECK(plant.device(0).kind == sim::DeviceKind::Belt);
    CHECK(plant.device(0).next == 2);  // the belt feeds the machine
    CHECK(plant.device(1).kind == sim::DeviceKind::PhotoEye);
    CHECK(plant.device(1).belt == 0);
    CHECK(plant.device(1).at > 0.0);
    CHECK(plant.device(2).kind == sim::DeviceKind::Machine);
    CHECK(plant.device(2).infeed == 0);
    CHECK(plant.device(2).next == 3);  // the machine discharges to the outfeed
    CHECK(plant.device(3).kind == sim::DeviceKind::Belt);
    CHECK(plant.device(4).kind == sim::DeviceKind::StackLight);
}

TEST(machine_consumes_a_box_after_its_cycle) {
    sim::Plant plant;
    const u32 belt = make_instant_belt(plant, "infeed", 1.0);
    const u32 machine = plant.add(sim::DeviceKind::Machine, "press");
    plant.device(machine).infeed = belt;
    plant.device(machine).cycle_seconds = 0.5;
    plant.device(belt).next = machine;
    plant.set_output(belt, true);
    plant.set_output(machine, true);
    plant.spawn_box(belt);

    for (int tick = 0; tick < 200; ++tick) {
        plant.tick(0.01);
    }

    CHECK(plant.device(machine).done_count == 1);
    CHECK(plant.box_count() == 0);  // consumed, not left on a belt
    CHECK(plant.input(machine));    // ready for the next one
}

TEST(machine_holds_the_box_while_it_works) {
    sim::Plant plant;
    const u32 belt = make_instant_belt(plant, "infeed", 1.0);
    const u32 machine = plant.add(sim::DeviceKind::Machine, "press");
    plant.device(machine).infeed = belt;
    plant.device(machine).cycle_seconds = 1.0;
    plant.device(belt).next = machine;
    plant.set_output(belt, true);
    plant.set_output(machine, true);
    plant.spawn_box(belt);

    // The box reaches the end at t=1.0s and is taken; the cycle is still running.
    for (int tick = 0; tick < 120; ++tick) {
        plant.tick(0.01);
    }

    CHECK(plant.device(machine).busy);
    CHECK(plant.device(machine).done_count == 0);
    CHECK(!plant.input(machine));
}

TEST(machine_takes_nothing_until_it_is_enabled) {
    sim::Plant plant;
    const u32 belt = make_instant_belt(plant, "infeed", 1.0);
    const u32 machine = plant.add(sim::DeviceKind::Machine, "press");
    plant.device(machine).infeed = belt;
    plant.device(belt).next = machine;
    plant.set_output(belt, true);
    plant.spawn_box(belt);

    for (int tick = 0; tick < 200; ++tick) {
        plant.tick(0.01);
    }

    CHECK(plant.device(machine).done_count == 0);
    CHECK(plant.boxes_on(belt) == 1);
    // A box waiting for a disabled machine is not a jam.
    CHECK(!plant.device(belt).jammed);
    CHECK(plant.input(machine));
}

TEST(machine_releases_into_its_output_belt) {
    sim::Plant plant;
    const u32 belt = make_instant_belt(plant, "infeed", 1.0);
    const u32 machine = plant.add(sim::DeviceKind::Machine, "press");
    const u32 outfeed = make_instant_belt(plant, "outfeed", 2.0);
    plant.device(machine).infeed = belt;
    plant.device(machine).next = outfeed;
    plant.device(machine).cycle_seconds = 0.3;
    plant.device(belt).next = machine;
    plant.set_output(belt, true);
    plant.set_output(machine, true);

    plant.spawn_box(belt);
    for (int tick = 0; tick < 300; ++tick) {
        plant.tick(0.01);
    }

    CHECK(plant.device(machine).done_count == 1);
    CHECK(plant.boxes_on(outfeed) == 1);
    CHECK(plant.device(machine).jammed == false);
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
