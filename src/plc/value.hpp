#pragma once
// IEC 61131-3 elementary values. Integers are held in 64 bits internally but
// every store re-applies the declared width, so INT wraps at 16 bits exactly
// like a real PLC. That wrapping is a lesson, not a bug.
#include "core/types.hpp"

#include <string>
#include <string_view>

namespace sc::plc {

enum class ValueType : u8 {
    Void = 0,
    Bool,
    Sint,
    Int,
    Dint,
    Lint,
    Usint,
    Uint,
    Udint,
    Ulint,
    Real,
    Lreal,
    Time,
};

inline constexpr int kValueTypeCount = 13;

const char* value_type_name(ValueType type);
bool parse_value_type(std::string_view name, ValueType& out);
bool is_integer_type(ValueType type);
bool is_real_type(ValueType type);
bool is_numeric_type(ValueType type);

struct Value {
    ValueType type = ValueType::Void;
    union {
        bool b;
        i64 i;
        f64 f;
    };

    Value() : i(0) {}

    static Value boolean(bool v) {
        Value out;
        out.type = ValueType::Bool;
        out.b = v;
        return out;
    }
    static Value integer(i64 v) {
        Value out;
        out.type = ValueType::Int;
        out.i = v;
        return out;
    }
    static Value real(f64 v) {
        Value out;
        out.type = ValueType::Real;
        out.f = v;
        return out;
    }
    static Value time(i64 milliseconds) {
        Value out;
        out.type = ValueType::Time;
        out.i = milliseconds;
        return out;
    }

    bool as_bool() const;
    i64 as_int() const;
    f64 as_real() const;
    std::string to_string() const;
};

// Converts to `target`, applying IEC range and wrap rules on the way.
Value coerce(Value value, ValueType target);

}  // namespace sc::plc
