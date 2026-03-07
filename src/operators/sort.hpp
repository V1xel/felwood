#pragma once

#include "operators/operator.hpp"
#include "sql/ast.hpp"

#include <algorithm>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

namespace felwood {
    class SortOperator final : public Operator {
    public:
        SortOperator(std::unique_ptr<Operator> child, std::vector<OrderByKey> keys)
            : child_(std::move(child)), keys_(std::move(keys)), done_(false) {}

        void open() override {
            child_->open();
            done_ = false;
        }

        std::optional<Chunk> next() override {
            if (done_) return std::nullopt;
            done_ = true;

            std::vector<Chunk> chunks;
            while (auto c = child_->next()) chunks.push_back(std::move(*c));
            if (chunks.empty()) return std::nullopt;

            return sort_chunk(merge(std::move(chunks)));
        }

        void close() override { child_->close(); }

    private:
        static Chunk merge(std::vector<Chunk> chunks) {
            if (chunks.size() == 1) return std::move(chunks[0]);
            Chunk out;
            for (const auto& col : chunks[0].columns)
                out.columns.emplace_back(col.name, col.type);
            for (auto& chunk : chunks) {
                for (std::size_t r = 0; r < chunk.num_rows; ++r)
                    for (std::size_t c = 0; c < chunk.columns.size(); ++c)
                        out.columns[c].append(chunk.columns[c].get(r));
                out.num_rows += chunk.num_rows;
            }
            return out;
        }

        Chunk sort_chunk(Chunk chunk) {
            const std::size_t n = chunk.num_rows;
            std::vector<std::size_t> idx(n);
            std::iota(idx.begin(), idx.end(), 0);

            std::stable_sort(idx.begin(), idx.end(), [&](std::size_t a, std::size_t b) {
                for (const auto& key : keys_) {
                    auto col_idx = chunk.find_column(key.col);
                    if (!col_idx)
                        throw std::runtime_error("SortOperator: column not found: " + key.col);
                    int cmp = compare(chunk.columns[*col_idx].get(a),
                                      chunk.columns[*col_idx].get(b));
                    if (cmp != 0) return key.asc ? cmp < 0 : cmp > 0;
                }
                return false;
            });

            Chunk out;
            out.num_rows = n;
            for (const auto& col : chunk.columns) {
                Column c(col.name, col.type);
                for (std::size_t i = 0; i < n; ++i)
                    c.append(col.get(idx[i]));
                out.columns.push_back(std::move(c));
            }
            return out;
        }

        static int compare(const Value& a, const Value& b) {
            if (std::holds_alternative<std::string>(a) &&
                std::holds_alternative<std::string>(b)) {
                const auto& sa = std::get<std::string>(a);
                const auto& sb = std::get<std::string>(b);
                if (sa < sb) return -1;
                if (sa > sb) return  1;
                return 0;
            }
            double da = to_double(a), db = to_double(b);
            if (da < db) return -1;
            if (da > db) return  1;
            return 0;
        }

        static double to_double(const Value& v) {
            if (auto* i = std::get_if<int64_t>(&v)) return static_cast<double>(*i);
            if (auto* d = std::get_if<double>(&v))  return *d;
            return 0.0;
        }

        std::unique_ptr<Operator> child_;
        std::vector<OrderByKey>   keys_;
        bool                      done_;
    };
}
