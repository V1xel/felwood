#pragma once

#include "common/column.hpp"

#include <string>
#include <vector>
#include <optional>
#include <stdexcept>

namespace felwood {
    struct ColumnSchema {
        std::string name;
        DataType    type;
    };

    struct TableSchema {
        std::vector<ColumnSchema> columns;

        void add(std::string name, DataType type) {
            columns.push_back({std::move(name), type});
        }

        [[nodiscard]] std::size_t size() const noexcept { return columns.size(); }

        [[nodiscard]] std::optional<std::size_t>
        find(const std::string& name) const noexcept {
            for (std::size_t i = 0; i < columns.size(); ++i)
                if (columns[i].name == name) return i;
            return std::nullopt;
        }
    };

    class Table {
    public:
        std::string         name;
        TableSchema         schema;
        std::vector<Column> columns;

        Table(std::string n, TableSchema s)
            : name(std::move(n)), schema(std::move(s))
        {
            columns.reserve(schema.size());
            for (const auto& desc : schema.columns)
                columns.emplace_back(desc.name, desc.type);
        }

        [[nodiscard]] std::size_t num_rows() const noexcept {
            return columns.empty() ? 0 : columns[0].size();
        }

        [[nodiscard]] std::size_t num_cols() const noexcept {
            return columns.size();
        }

        void append_row(const std::vector<Value>& row) {
            if (row.size() != columns.size())
                throw std::runtime_error("append_row: row width != schema width");
            for (std::size_t i = 0; i < columns.size(); ++i)
                columns[i].append(row[i]);
        }

        [[nodiscard]] const Column& get_column(const std::string& col_name) const {
            auto idx = schema.find(col_name);
            if (!idx) throw std::runtime_error("Table: column not found: " + col_name);
            return columns[*idx];
        }

        [[nodiscard]] const Column& get_column(std::size_t idx) const {
            return columns[idx];
        }
    };
}
