#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// felwood::AggregateOperator – hash aggregation with one GROUP BY column
//
// What this does:
//   This is a pipeline-breaking operator: it consumes ALL input from its child
//   before emitting any output (necessary because aggregation requires seeing
//   the entire partition before finalising results).
//
//   Algorithm:
//     open()  – open child, clear hash table.
//     next()  – first call: drain child into hash table, emit result Chunk.
//               subsequent calls: return nullopt.
//     close() – close child, free hash table.
//
//   The hash table maps a STRING group key to a vector of AggState objects,
//   one per aggregate specified in the AggSpec list.  After all input is
//   consumed, each state is finalised (SUM, COUNT, MIN, MAX, or AVG computed)
//   and written into output columns.  Insertion order is preserved so results
//   are deterministic.
//
//   Supported aggregate functions: SUM, COUNT, MIN, MAX, AVG.
//   Supported input column types for aggregation: INT64, FLOAT64.
//   GROUP BY column must be STRING.
//
// Where a real engine diverges:
//   • Multi-column GROUP BY: a composite key (hash of N columns) replaces the
//     single-string key; typically hashed with xxHash or HighwayHash.
//   • Two-phase aggregation for parallelism: each thread maintains a local
//     hash table; a global merge phase combines them (reduces contention).
//   • The hash table uses open addressing (Robin Hood / Swiss-table) instead
//     of std::unordered_map for better cache utilisation and lower overhead.
//   • SIMD-accelerated hashing: process 4–8 rows per cycle with AVX2.
//   • Spill-to-disk: when the hash table exceeds the memory budget, partitions
//     are spilled and re-processed (Grace hash aggregation).
//   • Partial / incremental aggregation: used in streaming contexts (sliding
//     windows) where not all input is available upfront.
//
// What to improve next:
//   • Replace std::unordered_map with a flat open-addressing hash table.
//   • Support multi-column GROUP BY keys.
//   • Add spill-to-disk with a configurable memory limit.
//   • Handle NULL inputs according to SQL semantics (COUNT(*) vs COUNT(col)).
// ─────────────────────────────────────────────────────────────────────────────

#include "operators/operator.hpp"

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <limits>
#include <stdexcept>
#include <cstdint>

namespace felwood {

// ── Aggregate function type ───────────────────────────────────────────────────
enum class AggFunc : uint8_t { SUM, COUNT, MIN, MAX, AVG };

// ── Specification for one aggregate ──────────────────────────────────────────
struct AggSpec {
    std::string input_col;   // source column name
    AggFunc     func;        // which aggregate function to apply
    std::string output_col;  // name of the result column in the output Chunk
};

// ── Per-group, per-aggregate accumulator ─────────────────────────────────────
// Stores running totals; values are always promoted to double.
// In a real engine: separate typed accumulators avoid precision loss
// (e.g., keep SUM as int64_t for integer columns).
struct AggState {
    double  sum       = 0.0;
    int64_t count     = 0;
    double  min_val   = std::numeric_limits<double>::max();
    double  max_val   = std::numeric_limits<double>::lowest();

    void update(double v) noexcept {
        sum += v;
        ++count;
        if (v < min_val) min_val = v;
        if (v > max_val) max_val = v;
    }

    [[nodiscard]] double finalize(AggFunc func) const noexcept {
        switch (func) {
            case AggFunc::SUM:   return sum;
            case AggFunc::COUNT: return static_cast<double>(count);
            case AggFunc::MIN:   return min_val;
            case AggFunc::MAX:   return max_val;
            case AggFunc::AVG:   return count > 0 ? sum / static_cast<double>(count) : 0.0;
        }
        return 0.0;
    }
};

// ── AggregateOperator ─────────────────────────────────────────────────────────
class AggregateOperator final : public Operator {
public:
    AggregateOperator(std::unique_ptr<Operator> child,
                      std::string               group_by_col,
                      std::vector<AggSpec>      agg_specs)
        : child_(std::move(child))
        , group_by_col_(std::move(group_by_col))
        , agg_specs_(std::move(agg_specs))
        , done_(false)
    {}

    void open() override {
        child_->open();
        hash_table_.clear();
        group_order_.clear();
        done_ = false;
    }

    std::optional<Chunk> next() override {
        if (done_) return std::nullopt;

        build_hash_table();   // consume all input
        done_ = true;
        return produce_result();
    }

    void close() override {
        child_->close();
        hash_table_.clear();
        group_order_.clear();
    }

private:
    // ── Phase 1: drain child and fill hash table ──────────────────────────
    void build_hash_table() {
        while (auto maybe_chunk = child_->next())
            accumulate(*maybe_chunk);
    }

    void accumulate(const Chunk& chunk) {
        // Resolve the group-by column (must be STRING).
        auto grp_idx = chunk.find_column(group_by_col_);
        if (!grp_idx)
            throw std::runtime_error(
                "AggregateOperator: group-by column not found: " + group_by_col_);

        const auto& grp_vec =
            std::get<std::vector<std::string>>(chunk.columns[*grp_idx].data);

        // Resolve input columns for each aggregate spec.
        // We cache type + raw pointer to avoid repeated lookup inside the row loop.
        struct AggColRef {
            DataType                     dtype;
            const std::vector<double>*   f64 = nullptr;
            const std::vector<int64_t>*  i64 = nullptr;
        };

        std::vector<AggColRef> refs;
        refs.reserve(agg_specs_.size());

        for (const auto& spec : agg_specs_) {
            auto col_idx = chunk.find_column(spec.input_col);
            if (!col_idx)
                throw std::runtime_error(
                    "AggregateOperator: aggregate column not found: " + spec.input_col);

            const Column& col = chunk.columns[*col_idx];
            AggColRef ref;
            ref.dtype = col.type;

            if (col.type == DataType::FLOAT64)
                ref.f64 = &std::get<std::vector<double>>(col.data);
            else if (col.type == DataType::INT64)
                ref.i64 = &std::get<std::vector<int64_t>>(col.data);
            else
                throw std::runtime_error(
                    "AggregateOperator: unsupported type for aggregation in column: "
                    + spec.input_col);

            refs.push_back(ref);
        }

        // Row-by-row accumulation.
        const std::size_t n_aggs = agg_specs_.size();

        for (std::size_t row = 0; row < chunk.num_rows; ++row) {
            const std::string& key = grp_vec[row];

            // try_emplace is O(1) amortised; returns existing entry if found.
            auto [it, inserted] =
                hash_table_.try_emplace(key, std::vector<AggState>(n_aggs));
            if (inserted)
                group_order_.push_back(key);  // record first-seen order

            auto& states = it->second;

            for (std::size_t ai = 0; ai < n_aggs; ++ai) {
                double val = (refs[ai].dtype == DataType::FLOAT64)
                                 ? (*refs[ai].f64)[row]
                                 : static_cast<double>((*refs[ai].i64)[row]);
                states[ai].update(val);
            }
        }
    }

    // ── Phase 2: materialise result Chunk ────────────────────────────────
    std::optional<Chunk> produce_result() {
        if (hash_table_.empty()) return std::nullopt;

        const std::size_t n = group_order_.size();
        Chunk out;
        out.num_rows = n;

        // Group-by key column (STRING).
        {
            Column col(group_by_col_, DataType::STRING);
            auto& vec = std::get<std::vector<std::string>>(col.data);
            vec.reserve(n);
            for (const auto& k : group_order_) vec.push_back(k);
            out.columns.push_back(std::move(col));
        }

        // One result column per AggSpec.
        for (std::size_t ai = 0; ai < agg_specs_.size(); ++ai) {
            const auto& spec = agg_specs_[ai];

            if (spec.func == AggFunc::COUNT) {
                // COUNT is always a non-negative integer.
                Column col(spec.output_col, DataType::INT64);
                auto& vec = std::get<std::vector<int64_t>>(col.data);
                vec.reserve(n);
                for (const auto& k : group_order_)
                    vec.push_back(hash_table_.at(k)[ai].count);
                out.columns.push_back(std::move(col));
            } else {
                // SUM / MIN / MAX / AVG → FLOAT64.
                Column col(spec.output_col, DataType::FLOAT64);
                auto& vec = std::get<std::vector<double>>(col.data);
                vec.reserve(n);
                for (const auto& k : group_order_)
                    vec.push_back(hash_table_.at(k)[ai].finalize(spec.func));
                out.columns.push_back(std::move(col));
            }
        }

        return out;
    }

    // ── Members ───────────────────────────────────────────────────────────
    std::unique_ptr<Operator> child_;
    std::string               group_by_col_;
    std::vector<AggSpec>      agg_specs_;

    // Hash table: group key → per-aggregate accumulators.
    std::unordered_map<std::string, std::vector<AggState>> hash_table_;

    // Preserve first-seen insertion order for deterministic output.
    std::vector<std::string> group_order_;

    bool done_;  // true after the first (and only) result chunk is emitted
};

} // namespace felwood
