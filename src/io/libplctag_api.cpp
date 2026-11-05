#include "io/libplctag_api.hpp"

#if defined(SC_WITH_LIBPLCTAG)
#include <libplctag.h>

#include <cstdint>
#include <string>
#endif

namespace sc::io {

#if defined(SC_WITH_LIBPLCTAG)
namespace {

std::string error_text(int code) {
    const char* text = plc_tag_decode_error(code);
    return text == nullptr ? std::string("unknown libplctag error") : std::string(text);
}

class LibPlcTagApi final : public PlcTagApi {
public:
    i32 create(std::string_view tag, int timeout_ms, std::string& error) override {
        const std::string text(tag);
        const std::int32_t handle = plc_tag_create(text.c_str(), timeout_ms);
        if (handle < 0) {
            error = "plc_tag_create failed: " + error_text(handle);
            return handle;
        }
        const int status = plc_tag_status(handle);
        if (status != PLCTAG_STATUS_OK) {
            error = "tag creation did not finish: " + error_text(status);
            plc_tag_destroy(handle);
            return -1;
        }
        return handle;
    }

    bool read(i32 handle, int timeout_ms, std::string& error) override {
        const int status = plc_tag_read(handle, timeout_ms);
        if (status != PLCTAG_STATUS_OK) {
            error = "plc_tag_read failed: " + error_text(status);
            return false;
        }
        return true;
    }

    bool get_bool(i32 handle, bool& value, std::string& error) override {
        const int raw = plc_tag_get_bit(handle, 0);
        if (raw < 0) {
            error = "plc_tag_get_bit failed: " + error_text(raw);
            return false;
        }
        value = raw != 0;
        return true;
    }

    bool set_bool(i32 handle, bool value, std::string& error) override {
        const int status = plc_tag_set_bit(handle, 0, value ? 1 : 0);
        if (status != PLCTAG_STATUS_OK) {
            error = "plc_tag_set_bit failed: " + error_text(status);
            return false;
        }
        return true;
    }

    bool write(i32 handle, int timeout_ms, std::string& error) override {
        const int status = plc_tag_write(handle, timeout_ms);
        if (status != PLCTAG_STATUS_OK) {
            error = "plc_tag_write failed: " + error_text(status);
            return false;
        }
        return true;
    }

    void destroy(i32 handle) override { (void)plc_tag_destroy(handle); }
};

}  // namespace

std::unique_ptr<PlcTagApi> make_libplctag_api(std::string& error) {
    error.clear();
    if (plc_tag_check_lib_version(2, 1, 0) != PLCTAG_STATUS_OK) {
        error = "libplctag 2.1.0 or newer is required";
        return nullptr;
    }
    return std::make_unique<LibPlcTagApi>();
}
#else
std::unique_ptr<PlcTagApi> make_libplctag_api(std::string& error) {
    error = "libplctag support was not enabled at build time "
            "(configure with -DSC_ENABLE_LIBPLCTAG=ON)";
    return nullptr;
}
#endif

}  // namespace sc::io
