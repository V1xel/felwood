#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// felwood::column – typed contiguous storage and the Chunk batch abstraction
//
// What this does:
//   A Column holds one std::vector per physical type (four alternatives via
//   std::variant).  All elements share the same type; mixing is a bug.
//   A Chunk is an ordered collection of named columns plus a row count.
//   BATCH_SIZE controls how many rows flow through the pipeline at once.
//
// Where a real engine diverges:
//   • Columns use arena-allocated, cache-line-aligned buffers instead of
//     std::vector, so memory is predictable and SIMD-friendly.
//   • A separate null bitmap (one bit per row) tracks NULLs without wasting a
//     full byte or sentinel value.
//   • Strings are stored as offsets+lengths into a shared byte buffer (Arrow
//     BinaryArray layout) to avoid per-string heap allocations.
//   • Dictionary encoding: a small integer index column + a dictionary vector
//     cuts memory for low-cardinality strings by 10-100×.
//   • BATCH_SIZE is typically tuned to L1/L2 cache per worker thread at
//     compile- or runtime rather than a hard constant.
//
// What to improve next:
//   • Add a NullBitmap member and propagate it through all operators.
//   • Add an arena allocator so consecutive batch allocations are bump-pointer.
//   • Benchmark different BATCH_SIZE values (512, 2048, 4096) on target CPUs.
// ─────────────────────────────────────────────────────────────────────────────

#include "common/types.hpp"

#include <vector>
#include <string>
#include <variant>
#include <optional>
#include <stdexcept>
#include <algorithm>

namespace felwood {

// ── Batch size (rows per chunk) ───────────────────────────────────────────────
// 1 024 rows × 8 bytes/elem fits two INT64 columns in a typical 16 KiB L1 cache.
inline constexpr std::size_t BATCH_SIZE = 1024;

// ── Physical storage: one vector per supported type ───────────────────────────
using ColumnData = std::variant<
    std::vector<int64_t>,   // INT64
    std::vector<double>,    // FLOAT64
    std::vector<std::string>, // STRING
    std::vector<bool>       // BOOLEAN  (std::vector<bool> is bit-packed)
>;

// ── Column ────────────────────────────────────────────────────────────────────
struct Column {
    std::string name;
    DataType    type;
    ColumnData  data;

    explicit Column(std::string n, DataType t)
        : name(std::move(n)), type(t)
    {
        // Initialise the variant to the correct vector type.
        switch (t) {
            case DataType::INT64:   data = std::vector<int64_t>{};   break;
            case DataType::FLOAT64: data = std::vector<double>{};    break;
            case DataType::STRING:  data = std::vector<std::string>{}; break;
            case DataType::BOOLEAN: data = std::vector<bool>{};      break;
        }
    }

    // Number of stored elements.
    [[nodiscard]] std::size_t size() const noexcept {
        return std::visit([](const auto& v) { return v.size(); }, data);
    }

    // Append one value; throws if type mismatch.
    void append(const Value& val) {
        std::visit([&](auto& vec) {
            using VecT  = std::decay_t<decltype(vec)>;
            using ElemT = typename VecT::value_type;
            vec.push_back(std::get<ElemT>(val));
        }, data);
    }

    // Random-access read; returns a Value wrapping the element.
    [[nodiscard]] Value get(std::size_t idx) const {
        return std::visit([idx](const auto& vec) -> Value {
            using ElemT = typename std::decay_t<decltype(vec)>::value_type;
            return Value{ static_cast<ElemT>(vec[idx]) };
        }, data);
    }
};

// ── Chunk ─────────────────────────────────────────────────────────────────────
// The unit of data that flows between operators in the Volcano pipeline.
struct Chunk {
    std::vector<Column> columns;
    std::size_t         num_rows = 0;

    Chunk() = default;
    Chunk(std::vector<Column> cols, std::size_t rows)
        : columns(std::move(cols)), num_rows(rows) {}

    [[nodiscard]] bool empty() const noexcept { return num_rows == 0; }

    // Linear scan by name; returns index or nullopt.
    [[nodiscard]] std::optional<std::size_t>
    find_column(const std::string& n) const noexcept {
        for (std::size_t i = 0; i < columns.size(); ++i)
            if (columns[i].name == n) return i;
        return std::nullopt;
    }

    // Throws if not found.
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
