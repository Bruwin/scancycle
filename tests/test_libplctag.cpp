#include "harness.hpp"

#include "io/commissioning.hpp"
#include "io/libplctag_api.hpp"
#include "io/plc_tag_driver.hpp"
#include "plc/compiler.hpp"
#include "plc/parser.hpp"

#include <memory>
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

// libplctag tags are whole attribute strings, so the driver never parses them.
const char* kStartTag = "protocol=ab-eip&gateway=192.168.1.10&name=Start";
const char* kRunTag = "protocol=ab-eip&gateway=192.168.1.10&name=Run";

struct FakeTag {
    std::string tag;
    bool value = false;   // what the remote PLC holds
    bool staged = false;  // set_bool buffer, sent by write()
    bool alive = true;
    int writes = 0;
};

class FakePlcTagApi final : public io::PlcTagApi {
public:
    i32 create(std::string_view tag, int, std::string& error) override {
        if (fail_after >= 0 && static_cast<int>(tags.size()) >= fail_after) {
            error = "simulated creation failure";
            return -1;
        }
        tags.push_back(FakeTag{std::string(tag)});
        ++creates;
        return static_cast<i32>(tags.size());
    }

    bool read(i32 handle, int, std::string& error) override {
        if (!valid(handle, error)) {
            return false;
        }
        ++reads;
        return true;
    }

    bool get_bool(i32 handle, bool& value, std::string& error) override {
        if (!valid(handle, error)) {
            return false;
        }
        value = tags[static_cast<std::size_t>(handle - 1)].value;
        return true;
    }

    bool set_bool(i32 handle, bool value, std::string& error) override {
        if (!valid(handle, error)) {
            return false;
        }
        tags[static_cast<std::size_t>(handle - 1)].staged = value;
        return true;
    }

    bool write(i32 handle, int, std::string& error) override {
        if (!valid(handle, error)) {
            return false;
        }
        FakeTag& tag = tags[static_cast<std::size_t>(handle - 1)];
        tag.value = tag.staged;
        ++tag.writes;
        ++writes;
        return true;
    }

    void destroy(i32 handle) override {
        if (handle <= 0 || static_cast<std::size_t>(handle) > tags.size()) {
            return;
        }
        FakeTag& tag = tags[static_cast<std::size_t>(handle - 1)];
        if (tag.alive) {
            tag.alive = false;
            ++destroys;
        }
    }

    bool valid(i32 handle, std::string& error) const {
        if (handle <= 0 || static_cast<std::size_t>(handle) > tags.size()) {
            error = "unknown tag handle";
            return false;
        }
        if (!tags[static_cast<std::size_t>(handle - 1)].alive) {
            error = "tag was already destroyed";
            return false;
        }
        return true;
    }

    int fail_after = -1;  // creation index that fails; negative never fails
    int creates = 0;
    int destroys = 0;
    int reads = 0;
    int writes = 0;
    std::vector<FakeTag> tags;
};

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

io::LibPlcTagConfig make_config(io::CommissioningPolicy* policy) {
    io::LibPlcTagConfig config;
    config.timeout_ms = 250;
    config.tags = {
        io::PlcTagBinding{"start", kStartTag, io::PlcTagDirection::Input},
        io::PlcTagBinding{"run", kRunTag, io::PlcTagDirection::Output},
    };
    config.commissioning = policy;
    return config;
}

}  // namespace

TEST(libplctag_driver_latches_inputs_and_writes_changed_outputs) {
    plc::CompiledProgram program;
    CHECK(compile_wire(program));
    if (program.code.empty()) {
        return;
    }
    plc::Vm vm(program);

    auto api = std::make_unique<FakePlcTagApi>();
    FakePlcTagApi* fake = api.get();

    io::CommissioningPolicy policy;
    policy.allow_address("run");
    policy.set_min_write_interval_ms(0);
    CHECK(policy.begin_session(0, 10'000));

    io::LibPlcTagDriver driver(make_config(&policy), vm, std::move(api));
    CHECK(driver.connect());
    if (!driver.connected()) {
        return;
    }
    CHECK(fake->creates == 2);
    CHECK(fake->tags[0].tag == kStartTag);

    fake->tags[0].value = true;
    CHECK(driver.run_scan(0));
    CHECK(vm.get("run").as_bool());
    CHECK(fake->tags[1].value);
    CHECK(fake->writes == 1);
    CHECK(policy.writes_permitted() == 1);

    // An unchanged output is not pushed to the PLC again.
    CHECK(driver.run_scan(10));
    CHECK(fake->writes == 1);

    fake->tags[0].value = false;
    CHECK(driver.run_scan(20));
    CHECK(!vm.get("run").as_bool());
    CHECK(!fake->tags[1].value);
    CHECK(fake->writes == 2);
    CHECK(driver.scans() == 3);

    // The scan latching contract: the program sees the input image of this scan.
    CHECK(fake->reads == 3);

    driver.disconnect();
    CHECK(!driver.connected());
    CHECK(fake->destroys == 2);
}

TEST(libplctag_driver_fails_closed_without_commissioning) {
    plc::CompiledProgram program;
    CHECK(compile_wire(program));
    if (program.code.empty()) {
        return;
    }
    plc::Vm vm(program);

    auto api = std::make_unique<FakePlcTagApi>();
    FakePlcTagApi* fake = api.get();
    fake->tags.reserve(2);

    io::LibPlcTagDriver driver(make_config(nullptr), vm, std::move(api));
    CHECK(driver.connect());
    if (!driver.connected()) {
        return;
    }

    fake->tags[0].value = true;
    CHECK(!driver.run_scan(0));
    CHECK(driver.last_error().find("commissioning") != std::string::npos);
    CHECK(fake->writes == 0);
    CHECK(!fake->tags[1].value);
}

TEST(libplctag_driver_destroys_created_tags_when_connect_fails) {
    plc::CompiledProgram program;
    CHECK(compile_wire(program));
    if (program.code.empty()) {
        return;
    }
    plc::Vm vm(program);

    auto api = std::make_unique<FakePlcTagApi>();
    FakePlcTagApi* fake = api.get();
    fake->fail_after = 1;

    io::LibPlcTagDriver driver(make_config(nullptr), vm, std::move(api));
    CHECK(!driver.connect());
    CHECK(!driver.connected());
    CHECK(fake->creates == 1);
    CHECK(fake->destroys == 1);
    CHECK(driver.last_error().find("could not create tag 'run'") != std::string::npos);
    CHECK(driver.last_error().find("simulated creation failure") != std::string::npos);
}

TEST(libplctag_driver_reports_a_missing_api) {
    plc::CompiledProgram program;
    CHECK(compile_wire(program));
    if (program.code.empty()) {
        return;
    }
    plc::Vm vm(program);

    io::LibPlcTagDriver driver(make_config(nullptr), vm, nullptr);
    CHECK(!driver.connect());
    CHECK(!driver.connected());
    CHECK(driver.last_error().find("unavailable") != std::string::npos);
}

TEST(libplctag_factory_diagnoses_a_missing_library) {
    std::string error;
    std::unique_ptr<io::PlcTagApi> api = io::make_libplctag_api(error);

    // Either the optional binding is compiled in and can create tags, or the
    // build-time diagnostic names the dependency and the switch that adds it.
    if (api) {
        CHECK(error.empty());
    } else {
        CHECK(error.find("libplctag") != std::string::npos);
        CHECK(error.find("SC_ENABLE_LIBPLCTAG") != std::string::npos);
    }
}
