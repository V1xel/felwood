#pragma once

#include "operators/operator.hpp"

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <limits>
#include <stdexcept>
#include <cstdint>

namespace felwood {

struct AggSpec {
    std::string input_col;
    AggFunc     func;
    std::string output_col;
};

struct AggState {
    double  sum     = 0.0;
    int64_t count   = 0;
    double  min_val = std::numeric_limits<double>::max();
    double  max_val = std::numeric_limits<double>::lowest();

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

        build_hash_table();
        done_ = true;
        return produce_result();
    }

    void close() override {
        child_->close();
        hash_table_.clear();
        group_order_.clear();
    }

private:
    void build_hash_table() {
        while (auto maybe_chunk = child_->next())
            accumulate(*maybe_chunk);
    }

    void accumulate(const Chunk& chunk) {
        auto grp_idx = chunk.find_column(group_by_col_);
        if (!grp_idx)
            throw std::runtime_error(
                "AggregateOperator: group-by column not found: " + group_by_col_);

        const auto& grp_vec =
            std::get<std::vector<std::string>>(chunk.columns[*grp_idx].data);

        struct AggColRef {
            DataType                    dtype;
            const std::vector<double>*  f64 = nullptr;
            const std::vector<int64_t>* i64 = nullptr;
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

        const std::size_t n_aggs = agg_specs_.size();

        for (std::size_t row = 0; row < chunk.num_rows; ++row) {
            const std::string& key = grp_vec[row];

            auto [it, inserted] =
                hash_table_.try_emplace(key, std::vector<AggState>(n_aggs));
            if (inserted)
                group_order_.push_back(key);

            auto& states = it->second;

            for (std::size_t ai = 0; ai < n_aggs; ++ai) {
                double val = (refs[ai].dtype == DataType::FLOAT64)
                                 ? (*refs[ai].f64)[row]
                                 : static_cast<double>((*refs[ai].i64)[row]);
                states[ai].update(val);
            }
        }
    }

    std::optional<Chunk> produce_result() {
        if (hash_table_.empty()) return std::nullopt;

        const std::size_t n = group_order_.size();
        Chunk out;
        out.num_rows = n;

        {
            Column col(group_by_col_, DataType::STRING);
            auto& vec = std::get<std::vector<std::string>>(col.data);
            vec.reserve(n);
            for (const auto& k : group_order_) vec.push_back(k);
            out.columns.push_back(std::move(col));
        }

        for (std::size_t ai = 0; ai < agg_specs_.size(); ++ai) {
            const auto& spec = agg_specs_[ai];

            if (spec.func == AggFunc::COUNT) {
                Column col(spec.output_col, DataType::INT64);
                auto& vec = std::get<std::vector<int64_t>>(col.data);
                vec.reserve(n);
                for (const auto& k : group_order_)
                    vec.push_back(hash_table_.at(k)[ai].count);
                out.columns.push_back(std::move(col));
            } else {
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

    std::unique_ptr<Operator> child_;
    std::string               group_by_col_;
    std::vector<AggSpec>      agg_specs_;

    std::unordered_map<std::string, std::vector<AggState>> hash_table_;
    std::vector<std::string>                               group_order_;

    bool done_;
};

} // namespace felwood
