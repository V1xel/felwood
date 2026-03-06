#pragma once

#include <cstdint>
#include <string>
#include <variant>
#include <string_view>
#include <stdexcept>

namespace felwood {

enum class DataType : uint8_t {
    INT64   = 0,
    FLOAT64 = 1,
    STRING  = 2,
    BOOLEAN = 3,
};

using Value = std::variant<
    int64_t,
    double,
    std::string,
    bool
>;

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

enum class AggFunc : uint8_t { SUM, COUNT, MIN, MAX, AVG };

inline std::string value_to_string(const Value& v) {
    return std::visit([](const auto& x) -> std::string {
        using T = std::decay_t<decltype(x)>;
        if constexpr (std::is_same_v<T, int64_t>)     return std::to_string(x);
        if constexpr (std::is_same_v<T, double>)      return std::to_string(x);
        if constexpr (std::is_same_v<T, std::string>) return x;
        if constexpr (std::is_same_v<T, bool>)        return x ? "true" : "false";
    }, v);
}

} // namespace felwood
