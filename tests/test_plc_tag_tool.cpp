#include "harness.hpp"

#include "app/plc_tag_tool.hpp"
#include "io/commissioning.hpp"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace sc;

namespace {

const char* kTag = "protocol=ab-eip&gateway=192.168.1.10&name=Start";

struct FakeTag {
    explicit FakeTag(std::string text, bool initial)
        : tag(std::move(text)), value(initial) {}

    std::string tag;
    bool value = false;
    bool staged = false;
    bool alive = true;
    bool read_since_create = false;
    bool staged_after_read = false;
    int writes = 0;
};

class FakePlcTagApi final : public io::PlcTagApi {
public:
    i32 create(std::string_view tag, int, std::string& error) override {
        if (fail_create) {
            error = "simulated creation failure";
            return -1;
        }
        tags.emplace_back(std::string(tag), initial_value);
        ++creates;
        return static_cast<i32>(tags.size());
    }

    bool read(i32 handle, int, std::string& error) override {
        if (!valid(handle, error)) {
            return false;
        }
        if (fail_read) {
            error = "simulated read failure";
            return false;
        }
        ++reads;
        tags[static_cast<std::size_t>(handle - 1)].read_since_create = true;
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
        FakeTag& tag = tags[static_cast<std::size_t>(handle - 1)];
        tag.staged = value;
        tag.staged_after_read = tag.read_since_create;
        return true;
    }

    bool write(i32 handle, int, std::string& error) override {
        if (!valid(handle, error)) {
            return false;
        }
        if (fail_write) {
            error = "simulated write failure";
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

    bool initial_value = true;
    bool fail_create = false;
    bool fail_read = false;
    bool fail_write = false;
    int creates = 0;
    int destroys = 0;
    int reads = 0;
    int writes = 0;
    std::vector<FakeTag> tags;
};

app::PlcTagToolConfig make_config() {
    app::PlcTagToolConfig config;
    config.tag = kTag;
    config.timeout_ms = 250;
    return config;
}

}  // namespace

TEST(plc_tag_tool_reads_a_boolean_and_destroys_the_tag) {
    FakePlcTagApi api;
    const app::PlcTagToolConfig config = make_config();

    const app::PlcTagToolResult result = app::plc_tag_read(config, api);
    CHECK(result.ok);
    CHECK(result.value);
    CHECK(api.creates == 1);
    CHECK(api.reads == 1);
    CHECK(api.destroys == 1);
}

TEST(plc_tag_tool_write_reads_before_staging_and_writes) {
    FakePlcTagApi api;
    const app::PlcTagToolConfig config = make_config();

    const app::PlcTagToolResult result = app::plc_tag_write(config, api, false);
    CHECK(result.ok);
    CHECK(!result.value);
    CHECK(api.reads == 1);
    CHECK(api.writes == 1);
    CHECK(api.tags[0].staged_after_read);
    CHECK(!api.tags[0].value);
    CHECK(api.destroys == 1);
}

TEST(plc_tag_tool_refuses_a_write_without_commissioning) {
    FakePlcTagApi api;
    app::PlcTagToolConfig config = make_config();
    io::CommissioningPolicy policy;  // No session and no allowlisted tag.
    config.commissioning = &policy;

    const app::PlcTagToolResult result = app::plc_tag_write(config, api, true);
    CHECK(!result.ok);
    CHECK(result.error.find("commissioning") != std::string::npos);
    CHECK(api.creates == 0);
    CHECK(api.writes == 0);
}

TEST(plc_tag_tool_reports_create_and_read_failures) {
    const app::PlcTagToolConfig config = make_config();

    FakePlcTagApi create_failure;
    create_failure.fail_create = true;
    const app::PlcTagToolResult create_result = app::plc_tag_read(config, create_failure);
    CHECK(!create_result.ok);
    CHECK(create_result.error.find("create") != std::string::npos);
    CHECK(create_result.error.find("simulated creation failure") != std::string::npos);
    CHECK(create_failure.destroys == 0);

    FakePlcTagApi read_failure;
    read_failure.fail_read = true;
    const app::PlcTagToolResult read_result = app::plc_tag_write(config, read_failure, true);
    CHECK(!read_result.ok);
    CHECK(read_result.error.find("before write") != std::string::npos);
    CHECK(read_result.error.find("simulated read failure") != std::string::npos);
    CHECK(read_failure.writes == 0);
    CHECK(read_failure.destroys == 1);
}

TEST(plc_tag_tool_reports_a_write_failure) {
    FakePlcTagApi api;
    api.fail_write = true;
    const app::PlcTagToolConfig config = make_config();

    const app::PlcTagToolResult result = app::plc_tag_write(config, api, true);
    CHECK(!result.ok);
    CHECK(result.error.find("write") != std::string::npos);
    CHECK(result.error.find("simulated write failure") != std::string::npos);
    CHECK(api.writes == 0);
    CHECK(api.destroys == 1);
}

TEST(plc_tag_tool_rejects_invalid_configuration) {
    FakePlcTagApi api;

    app::PlcTagToolConfig empty_tag = make_config();
    empty_tag.tag.clear();
    const app::PlcTagToolResult tag_result = app::plc_tag_read(empty_tag, api);
    CHECK(!tag_result.ok);
    CHECK(tag_result.error.find("tag") != std::string::npos);

    app::PlcTagToolConfig bad_timeout = make_config();
    bad_timeout.timeout_ms = 0;
    const app::PlcTagToolResult timeout_result = app::plc_tag_read(bad_timeout, api);
    CHECK(!timeout_result.ok);
    CHECK(timeout_result.error.find("timeout") != std::string::npos);
    CHECK(api.creates == 0);
}
