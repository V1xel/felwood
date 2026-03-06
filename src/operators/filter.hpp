#pragma once

#include "operators/operator.hpp"

#include <functional>
#include <memory>
#include <vector>

namespace felwood {

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
            if (!maybe_chunk) return std::nullopt;

            Chunk& chunk = *maybe_chunk;

            std::vector<std::size_t> sel;
            sel.reserve(chunk.num_rows);

            for (std::size_t i = 0; i < chunk.num_rows; ++i)
                if (pred_(chunk, i))
                    sel.push_back(i);

            if (sel.empty()) continue;

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
