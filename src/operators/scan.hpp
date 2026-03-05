#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// felwood::ScanOperator – sequential table scan with column projection
//
// What this does:
//   Reads a Table in slices of up to BATCH_SIZE rows and emits one Chunk per
//   slice.  Column projection is applied eagerly: only the requested columns
//   are copied, so downstream operators never see—or pay for—unrequested ones.
//
// Where a real engine diverges:
//   • Reads from disk via async I/O (io_uring on Linux) or memory-mapped files;
//     the scan overlaps I/O and CPU processing.
//   • Columnar storage formats (Parquet, ORC, custom) store pages in compressed
//     form; the scan decompresses only the needed pages with SIMD routines.
//   • Page-level statistics (min/max zone maps, Bloom filters) allow the scan
//     to skip entire pages without decompressing them (predicate pushdown).
//   • Late materialisation: inexpensive filter columns are decoded first;
//     expensive string columns are decoded only for rows that survive the filter.
//   • For a partitioned table the scan is parallelised across worker threads,
//     each thread processing an independent range of row groups.
//
// What to improve next:
//   • Implement iterator-based access to an on-disk Parquet reader.
//   • Add per-column skip logic driven by zone-map statistics.
//   • Support asynchronous prefetching (double-buffered batches).
// ─────────────────────────────────────────────────────────────────────────────

#include "operators/operator.hpp"
#include "storage/table.hpp"

#include <vector>
#include <string>
#include <algorithm>
#include <numeric>
#include <stdexcept>

namespace felwood {

class ScanOperator final : public Operator {
public:
    // project_cols: subset of column names to emit; empty = all columns.
    explicit ScanOperator(const Table& table,
                          std::vector<std::string> project_cols = {})
        : table_(table)
        , project_cols_(std::move(project_cols))
        , current_row_(0)
    {}

    void open() override {
        current_row_ = 0;
        projected_indices_.clear();

        if (project_cols_.empty()) {
            projected_indices_.resize(table_.num_cols());
            std::iota(projected_indices_.begin(), projected_indices_.end(), 0);
        } else {
            projected_indices_.reserve(project_cols_.size());
            for (const auto& col_name : project_cols_) {
                auto idx = table_.schema.find(col_name);
                if (!idx)
                    throw std::runtime_error(
                        "ScanOperator: projected column not found: " + col_name);
                projected_indices_.push_back(*idx);
            }
        }
    }

    std::optional<Chunk> next() override {
        const std::size_t total = table_.num_rows();
        if (current_row_ >= total) return std::nullopt;

        const std::size_t batch_end  = std::min(current_row_ + BATCH_SIZE, total);
        const std::size_t batch_size = batch_end - current_row_;

        // Build output chunk: one Column per projected column.
        std::vector<Column> out_cols;
        out_cols.reserve(projected_indices_.size());

        for (std::size_t col_idx : projected_indices_) {
            const Column& src = table_.get_column(col_idx);
            Column dst(src.name, src.type);

            // Copy [current_row_, batch_end) from src into dst.
            std::visit([&](auto& dst_vec) {
                using VecT = std::decay_t<decltype(dst_vec)>;
                const auto& src_vec = std::get<VecT>(src.data);
                dst_vec.insert(dst_vec.end(),
                               src_vec.begin() + static_cast<std::ptrdiff_t>(current_row_),
                               src_vec.begin() + static_cast<std::ptrdiff_t>(batch_end));
            }, dst.data);

            out_cols.push_back(std::move(dst));
        }

        current_row_ = batch_end;
        return Chunk{ std::move(out_cols), batch_size };
    }

    void close() override {
        current_row_ = 0;
        projected_indices_.clear();
    }

private:
    const Table&             table_;
    std::vector<std::string> project_cols_;
    std::vector<std::size_t> projected_indices_;
    std::size_t              current_row_;
};

} // namespace felwood
