#include "plc/function_blocks.hpp"

#include <string>
#include <string_view>

namespace sc::plc {
namespace {

const FbDecl* fb_declarations() {
    static const FbDecl kDecls[] = {
        {"ton", {"in", "pt", "q", "et"}, 2},
        {"tof", {"in", "pt", "q", "et"}, 2},
        {"ctu", {"cu", "reset", "pv", "q", "cv"}, 2},
        {"ctd", {"cd", "load", "pv", "q", "cv"}, 2},
    };
    return kDecls;
}

constexpr std::size_t kFbDeclarationCount = 4;

}  // namespace

Builtin find_builtin_function(std::string_view name) {
    if (name == "abs") return Builtin::Abs;
    if (name == "min") return Builtin::Min;
    if (name == "max") return Builtin::Max;
    if (name == "limit") return Builtin::Limit;
    if (name == "sel") return Builtin::Sel;
    if (name == "mux") return Builtin::Mux;
    if (name == "sqrt") return Builtin::Sqrt;
    if (name == "expt") return Builtin::Expt;
    if (name == "trunc") return Builtin::Trunc;
    if (name == "round") return Builtin::Round;
    if (name == "shl") return Builtin::Shl;
    if (name == "shr") return Builtin::Shr;
    if (name == "rol") return Builtin::Rol;
    if (name == "ror") return Builtin::Ror;
    return Builtin::None;
}

const FbDecl* find_builtin_fb(std::string_view type_name) {
    const FbDecl* decls = fb_declarations();
    for (std::size_t i = 0; i < kFbDeclarationCount; ++i) {
        if (std::string_view(decls[i].type) == type_name) {
            return &decls[i];
        }
    }
    return nullptr;
}

bool is_fb_output_field(std::string_view type_name, std::string_view field) {
    if (type_name == "ton" || type_name == "tof") {
        return field == "q" || field == "et";
    }
    if (type_name == "ctu" || type_name == "ctd") {
        return field == "q" || field == "cv";
    }
    return false;
}

void update_function_block(const FbDecl& decl, std::vector<Value>& slots, i64 now_ms) {
    const std::string_view type(decl.type);

    if (type == "ton") {
        const bool in = slots[0].as_bool();
        const i64 preset = slots[1].as_int();
        const bool previous_in = slots[4].as_bool();
        i64 started_at = slots[5].as_int();

        bool q = false;
        i64 elapsed = 0;
        if (in) {
            if (!previous_in) {
                started_at = now_ms;
            }
            elapsed = now_ms - started_at;
            if (elapsed < 0) {
                elapsed = 0;
            }
            if (elapsed >= preset) {
                q = true;
                elapsed = preset;
            }
        }
        slots[2] = Value::boolean(q);
        slots[3] = Value::time(elapsed);
        slots[4] = Value::boolean(in);
        slots[5] = Value::integer(started_at);
        return;
    }

    if (type == "tof") {
        const bool in = slots[0].as_bool();
        const i64 preset = slots[1].as_int();
        const bool previous_in = slots[4].as_bool();
        i64 started_at = slots[5].as_int();

        bool q = false;
        i64 elapsed = 0;
        if (in) {
            q = true;
            started_at = now_ms;
        } else {
            if (previous_in) {
                started_at = now_ms;
            }
            elapsed = now_ms - started_at;
            if (elapsed < 0) {
                elapsed = 0;
            }
            if (elapsed >= preset) {
                q = false;
                elapsed = preset;
            } else {
                q = true;
            }
        }
        slots[2] = Value::boolean(q);
        slots[3] = Value::time(elapsed);
        slots[4] = Value::boolean(in);
        slots[5] = Value::integer(started_at);
        return;
    }

    if (type == "ctu") {
        const bool pulse = slots[0].as_bool();
        const bool reset = slots[1].as_bool();
        const i64 preset = slots[2].as_int();
        const bool previous_pulse = slots[5].as_bool();
        i64 count = slots[6].as_int();

        if (reset) {
            count = 0;
        } else if (pulse && !previous_pulse && count < preset) {
            ++count;
        }
        slots[3] = Value::boolean(count >= preset && count != 0);
        slots[4] = Value::integer(count);
        slots[5] = Value::boolean(pulse);
        slots[6] = Value::integer(count);
        return;
    }

    if (type == "ctd") {
        const bool pulse = slots[0].as_bool();
        const bool load = slots[1].as_bool();
        const i64 preset = slots[2].as_int();
        const bool previous_pulse = slots[5].as_bool();
        i64 count = slots[6].as_int();

        if (load) {
            count = preset;
        } else if (pulse && !previous_pulse && count > 0) {
            --count;
        }
        slots[3] = Value::boolean(count <= 0);
        slots[4] = Value::integer(count);
        slots[5] = Value::boolean(pulse);
        slots[6] = Value::integer(count);
    }
}

}  // namespace sc::plc
