#include "plc/value.hpp"

#include <cmath>
#include <cstdio>

namespace sc::plc {
namespace {

constexpr const char* kTypeNamesLower[kValueTypeCount] = {
    "void", "bool",  "sint", "int",  "dint", "lint", "usint",
    "uint", "udint", "ulint", "real", "lreal", "time",
};

constexpr const char* kTypeNamesUpper[kValueTypeCount] = {
    "VOID", "BOOL",  "SINT", "INT",  "DINT", "LINT", "USINT",
    "UINT", "UDINT", "ULINT", "REAL", "LREAL", "TIME",
};

}  // namespace

const char* value_type_name(ValueType type) {
    const int index = static_cast<int>(type);
    if (index < 0 || index >= kValueTypeCount) {
        return "VOID";
    }
    return kTypeNamesUpper[index];
}

bool parse_value_type(std::string_view name, ValueType& out) {
    for (int i = 0; i < kValueTypeCount; ++i) {
        if (name == kTypeNamesLower[i]) {
            out = static_cast<ValueType>(i);
            return true;
        }
    }
    return false;
}

bool is_integer_type(ValueType type) {
    switch (type) {
        case ValueType::Sint:
        case ValueType::Int:
        case ValueType::Dint:
        case ValueType::Lint:
        case ValueType::Usint:
        case ValueType::Uint:
        case ValueType::Udint:
        case ValueType::Ulint:
            return true;
        default:
            return false;
    }
}

bool is_real_type(ValueType type) { return type == ValueType::Real || type == ValueType::Lreal; }

bool is_numeric_type(ValueType type) { return is_integer_type(type) || is_real_type(type); }

bool Value::as_bool() const {
    switch (type) {
        case ValueType::Bool:
            return b;
        case ValueType::Real:
        case ValueType::Lreal:
            return f != 0.0;
        case ValueType::Void:
            return false;
        default:
            return i != 0;
    }
}

i64 Value::as_int() const {
    switch (type) {
        case ValueType::Bool:
            return b ? 1 : 0;
        case ValueType::Real:
        case ValueType::Lreal:
            return static_cast<i64>(f);
        case ValueType::Void:
            return 0;
        default:
            return i;
    }
}

f64 Value::as_real() const {
    switch (type) {
        case ValueType::Bool:
            return b ? 1.0 : 0.0;
        case ValueType::Real:
        case ValueType::Lreal:
            return f;
        case ValueType::Void:
            return 0.0;
        default:
            return static_cast<f64>(i);
    }
}

std::string Value::to_string() const {
    char buffer[64];
    switch (type) {
        case ValueType::Bool:
            return b ? "TRUE" : "FALSE";
        case ValueType::Real:
        case ValueType::Lreal:
            std::snprintf(buffer, sizeof(buffer), "%g", f);
            return buffer;
        case ValueType::Time:
            std::snprintf(buffer, sizeof(buffer), "T#%lldms", static_cast<long long>(i));
            return buffer;
        case ValueType::Void:
            return "<void>";
        default:
            std::snprintf(buffer, sizeof(buffer), "%lld", static_cast<long long>(i));
            return buffer;
    }
}

Value coerce(Value value, ValueType target) {
    if (target == ValueType::Void) {
        return value;
    }

    Value out;
    out.type = target;
    switch (target) {
        case ValueType::Bool:
            out.b = value.as_bool();
            break;
        case ValueType::Sint:
            out.i = static_cast<i64>(static_cast<i8>(value.as_int()));
            break;
        case ValueType::Int:
            out.i = static_cast<i64>(static_cast<i16>(value.as_int()));
            break;
        case ValueType::Dint:
            out.i = static_cast<i64>(static_cast<i32>(value.as_int()));
            break;
        case ValueType::Usint:
            out.i = static_cast<i64>(static_cast<u8>(value.as_int()));
            break;
        case ValueType::Uint:
            out.i = static_cast<i64>(static_cast<u16>(value.as_int()));
            break;
        case ValueType::Udint:
            out.i = static_cast<i64>(static_cast<u32>(value.as_int()));
            break;
        case ValueType::Real:
        case ValueType::Lreal:
            out.f = value.as_real();
            break;
        case ValueType::Time:
        case ValueType::Lint:
        case ValueType::Ulint:
        case ValueType::Void:
            out.i = value.as_int();
            break;
    }
    return out;
}

}  // namespace sc::plc
