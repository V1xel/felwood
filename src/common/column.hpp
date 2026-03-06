#pragma once

#include "common/types.hpp"

#include <vector>
#include <string>
#include <variant>
#include <optional>
#include <stdexcept>
#include <algorithm>

namespace felwood {

inline constexpr std::size_t BATCH_SIZE = 1024;

using ColumnData = std::variant<
    std::vector<int64_t>,
    std::vector<double>,
    std::vector<std::string>,
    std::vector<bool>
>;

struct Column {
    std::string name;
    DataType    type;
    ColumnData  data;

    explicit Column(std::string n, DataType t)
        : name(std::move(n)), type(t)
    {
        switch (t) {
            case DataType::INT64:   data = std::vector<int64_t>{};    break;
            case DataType::FLOAT64: data = std::vector<double>{};     break;
            case DataType::STRING:  data = std::vector<std::string>{}; break;
            case DataType::BOOLEAN: data = std::vector<bool>{};       break;
        }
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return std::visit([](const auto& v) { return v.size(); }, data);
    }

    void append(const Value& val) {
        std::visit([&](auto& vec) {
            using VecT  = std::decay_t<decltype(vec)>;
            using ElemT = typename VecT::value_type;
            vec.push_back(std::get<ElemT>(val));
        }, data);
    }

    [[nodiscard]] Value get(std::size_t idx) const {
        return std::visit([idx](const auto& vec) -> Value {
            using ElemT = typename std::decay_t<decltype(vec)>::value_type;
            return Value{ static_cast<ElemT>(vec[idx]) };
        }, data);
    }
};

struct Chunk {
    std::vector<Column> columns;
    std::size_t         num_rows = 0;

    Chunk() = default;
    Chunk(std::vector<Column> cols, std::size_t rows)
        : columns(std::move(cols)), num_rows(rows) {}

    [[nodiscard]] bool empty() const noexcept { return num_rows == 0; }

    [[nodiscard]] std::optional<std::size_t>
    find_column(const std::string& n) const noexcept {
        for (std::size_t i = 0; i < columns.size(); ++i)
            if (columns[i].name == n) return i;
        return std::nullopt;
    }

    [[nodiscard]] const Column& get_column(const std::string& n) const {
        auto idx = find_column(n);
        if (!idx) throw std::runtime_error("Column not found: " + n);
        return columns[*idx];
    }

    [[nodiscard]] Column& get_column(const std::string& n) {
        auto idx = find_column(n);
        if (!idx) throw std::runtime_error("Column not found: " + n);
        return columns[*idx];
    }
};

} // namespace felwood
