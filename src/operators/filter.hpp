#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// felwood::FilterOperator – predicate-based row filter with selection vector
//
// What this does:
//   For each input Chunk, evaluates a user-supplied Predicate for every row
//   to build a selection vector (the indices of passing rows).  It then
//   compacts the chunk by copying only those rows into the output.  Chunks
//   where zero rows pass are silently skipped; the operator loops to the
//   next input chunk automatically so callers never receive an empty Chunk.
//
// Where a real engine diverges:
//   • The predicate is compiled to a SIMD kernel (AVX-512 comparisons produce
//     64-bit bitmasks in a single instruction, testing all 64 rows at once).
//   • Selection vectors are represented as packed bitmaps rather than arrays
//     of indices; downstream operators can consume bitmaps directly without
//     materialisation.
//   • Late materialisation: expensive columns (e.g., variable-length strings)
//     are not decoded until after the bitmap is applied.
//   • In a JIT-compiled engine, the filter expression is inlined into the
//     scan loop, eliminating this operator class entirely.
//   • Bloom filters and zone-map checks are pushed down to the scan layer,
//     reducing the rows that even reach this operator.
//
// What to improve next:
//   • Replace the std::function predicate with a typed ExprNode AST that can
//     be compiled to SIMD or LLVM IR.
//   • Track a "selectivity" estimate to decide whether to compact eagerly or
//     defer using a bitmap.
// ─────────────────────────────────────────────────────────────────────────────

#include "operators/operator.hpp"

#include <functional>
#include <memory>
#include <vector>

namespace felwood {

// A predicate is called once per row; returns true if the row should be kept.
using Predicate = std::function<bool(const Chunk& chunk, std::size_t row_idx)>;

class FilterOperator final : public Operator {
public:
    explicit FilterOperator(std::unique_ptr<Operator> child, Predicate pred)
        : child_(std::move(child))
        , pred_(std::move(pred))
    {}

    void open() override { child_->open(); }

    std::optional<Chunk> next() override {
        while (true) {
            auto maybe_chunk = child_->next();
            if (!maybe_chunk) return std::nullopt;  // child exhausted

            Chunk& chunk = *maybe_chunk;

            // ── Build selection vector ──────────────────────────────────────
            // sel[i] = original row index of the i-th surviving row.
            std::vector<std::size_t> sel;
            sel.reserve(chunk.num_rows);

            for (std::size_t i = 0; i < chunk.num_rows; ++i)
                if (pred_(chunk, i))
                    sel.push_back(i);

            if (sel.empty()) continue;  // nothing passed; fetch next chunk

            // ── Compact: materialise only surviving rows ───────────────────
            Chunk out;
            out.num_rows = sel.size();
            out.columns.reserve(chunk.columns.size());

            for (const Column& src : chunk.columns) {
                Column dst(src.name, src.type);

                std::visit([&](auto& dst_vec) {
                    using VecT = std::decay_t<decltype(dst_vec)>;
                    const auto& src_vec = std::get<VecT>(src.data);
                    dst_vec.reserve(sel.size());
                    for (std::size_t row : sel)
                        dst_vec.push_back(static_cast<typename VecT::value_type>(src_vec[row]));
                }, dst.data);

                out.columns.push_back(std::move(dst));
            }

            return out;
        }
    }

    void close() override { child_->close(); }

private:
    std::unique_ptr<Operator> child_;
    Predicate                 pred_;
};

} // namespace felwood
