#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// felwood::Table – named collection of same-length typed columns
//
// What this does:
//   Table owns the ground-truth data for the engine.  Columns are stored
//   side-by-side (column-major layout) so a scan reads only the columns it
//   needs, skipping the rest entirely – the key advantage of columnar storage.
//   Schema captures column names and types; it acts as the metadata catalog
//   entry for a single relation.
//
// Where a real engine diverges:
//   • Data is stored on disk as compressed column groups (row groups in
//     Parquet, column chunks in ORC, or custom binary files).
//   • Each column group stores per-page statistics (min, max, null count,
//     bloom filter) enabling partition/page pruning before any data is read.
//   • A separate Buffer Manager (buffer pool) handles page eviction and
//     memory-mapping, abstracting disk vs memory from the query layer.
//   • A Catalog (e.g., backed by SQLite or an embedded KV store) maps table
//     names → file paths, schemas, and statistics.
//   • For distributed engines: the catalog also tracks data partitioning
//     (hash, range) and replica placement.
//
// What to improve next:
//   • Add a row-group partitioning layer so ScanOperator can skip whole
//     partitions using min/max statistics (zone maps).
//   • Support mutable tables: delta-store + immutable base (LSM-tree style).
// ─────────────────────────────────────────────────────────────────────────────

#include "common/column.hpp"

#include <string>
#include <vector>
#include <optional>
#include <stdexcept>

namespace felwood {

// ── Schema ────────────────────────────────────────────────────────────────────
// Lightweight metadata: ordered list of (name, type) pairs.
struct Schema {
    std::vector<std::string> names;
    std::vector<DataType>    types;

    void add(std::string name, DataType type) {
        names.push_back(std::move(name));
        types.push_back(type);
    }

    [[nodiscard]] std::size_t size() const noexcept { return names.size(); }

    [[nodiscard]] std::optional<std::size_t>
    find(const std::string& name) const noexcept {
        for (std::size_t i = 0; i < names.size(); ++i)
            if (names[i] == name) return i;
        return std::nullopt;
    }
};

// ── Table ─────────────────────────────────────────────────────────────────────
class Table {
public:
    std::string           name;
    Schema                schema;
    std::vector<Column>   columns; // one Column per schema entry, all equal length

    Table(std::string n, Schema s)
        : name(std::move(n)), schema(std::move(s))
    {
        columns.reserve(schema.size());
        for (std::size_t i = 0; i < schema.size(); ++i)
            columns.emplace_back(schema.names[i], schema.types[i]);
    }

    [[nodiscard]] std::size_t num_rows() const noexcept {
        return columns.empty() ? 0 : columns[0].size();
    }

    [[nodiscard]] std::size_t num_cols() const noexcept {
        return columns.size();
    }

    // Append one row supplied as a parallel vector of Values.
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

} // namespace felwood
