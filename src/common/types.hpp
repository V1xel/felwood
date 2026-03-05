#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// felwood::types – scalar type system
//
// What this does:
//   Defines the four runtime types the engine understands (INT64, FLOAT64,
//   STRING, BOOLEAN) and the tagged union `Value` that holds a single cell.
//
// Where a real engine diverges:
//   • Adds NULL (typically a separate validity bitmap, not a sentinel value).
//   • Adds DECIMAL, DATE32, TIMESTAMP64, LIST, STRUCT, …
//   • Type IDs are used to drive template dispatch (avoids virtual calls).
//   • Physical vs logical type distinction (e.g., DICT-encoded STRING still
//     has logical type STRING but physical type INT32 dictionary index).
//
// What to improve next:
//   • Null handling – wrap Value in std::optional or add a NullValue monostate.
//   • Decimal / fixed-point arithmetic for financial data.
//   • Arrow-compatible type IDs so we can zero-copy import Arrow record batches.
// ─────────────────────────────────────────────────────────────────────────────

#include <cstdint>
#include <string>
#include <variant>
#include <string_view>
#include <stdexcept>

namespace felwood {

// ── Physical type tag ─────────────────────────────────────────────────────────
enum class DataType : uint8_t {
    INT64   = 0,
    FLOAT64 = 1,
    STRING  = 2,
    BOOLEAN = 3,
};

// ── Single scalar value (tagged union) ───────────────────────────────────────
// Order must match DataType ordinals so index() == static_cast<size_t>(DataType).
using Value = std::variant<
    int64_t,      // INT64
    double,       // FLOAT64
    std::string,  // STRING
    bool          // BOOLEAN
>;

// ── Helpers ───────────────────────────────────────────────────────────────────

inline DataType value_type(const Value& v) noexcept {
    return static_cast<DataType>(v.index());
}

inline std::string_view type_name(DataType dt) noexcept {
    switch (dt) {
        case DataType::INT64:   return "INT64";
        case DataType::FLOAT64: return "FLOAT64";
        case DataType::STRING:  return "STRING";
        case DataType::BOOLEAN: return "BOOLEAN";
    }
    return "UNKNOWN";
}

// Convert a Value to a printable string (debug / display use only).
inline std::string value_to_string(const Value& v) {
    return std::visit([](const auto& x) -> std::string {
        using T = std::decay_t<decltype(x)>;
        if constexpr (std::is_same_v<T, int64_t>)    return std::to_string(x);
        if constexpr (std::is_same_v<T, double>)     return std::to_string(x);
        if constexpr (std::is_same_v<T, std::string>) return x;
        if constexpr (std::is_same_v<T, bool>)       return x ? "true" : "false";
    }, v);
}

} // namespace felwood
