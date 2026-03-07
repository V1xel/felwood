#pragma once

#include "sql/ast.hpp"
#include "storage/catalog.hpp"
#include "operators/scan.hpp"
#include "operators/filter.hpp"
#include "operators/aggregate.hpp"

#include <memory>
#include <optional>
#include <set>
#include <stdexcept>

namespace felwood {
    class Planner {
    public:
        explicit Planner(Catalog& catalog) : catalog_(catalog) {}

        std::optional<std::unique_ptr<Operator>> plan(const Stmt& stmt) {
            return std::visit([this](const auto& s) {
                return execute(s);
            }, stmt);
        }

    private:
        Catalog& catalog_;

        std::optional<std::unique_ptr<Operator>> execute(const CreateTableStmt& stmt) {
            TableSchema schema;
            for (const auto& col : stmt.cols)
                schema.add(col.name, col.type);
            catalog_.create_table(stmt.name, std::move(schema));
            return std::nullopt;
        }

        std::optional<std::unique_ptr<Operator>> execute(const InsertStmt& stmt) {
            catalog_.insert_row(stmt.table, stmt.values);
            return std::nullopt;
        }

        std::optional<std::unique_ptr<Operator>> execute(const SelectStmt& stmt) {
            const Table& table = catalog_.get_table(stmt.from);

            std::vector<std::string> cols;
            if (stmt.star) {
                for (const auto& c : table.schema.columns)
                    cols.push_back(c.name);
            } else {
                cols = collect_columns(stmt);
            }

            std::unique_ptr<Operator> op =
                std::make_unique<ScanOperator>(table, cols);

            if (!stmt.where.empty()) {
                auto conditions = stmt.where;
                op = std::make_unique<FilterOperator>(
                    std::move(op),
                    [conditions](const Chunk& chunk, std::size_t row) -> bool {
                        for (const auto& cond : conditions)
                            if (!eval_condition(chunk, row, cond)) return false;
                        return true;
                    }
                );
            }

            if (!stmt.group_by.empty()) {
                std::vector<AggSpec> specs;
                for (const auto& item : stmt.items)
                    if (item.agg)
                        specs.push_back({item.col, *item.agg, item.alias});
                op = std::make_unique<AggregateOperator>(
                    std::move(op), stmt.group_by[0], std::move(specs));
            }

            return op;
        }

        static std::vector<std::string> collect_columns(const SelectStmt& stmt) {
            std::set<std::string>    seen;
            std::vector<std::string> cols;

            auto add = [&](const std::string& name) {
                if (seen.insert(name).second) cols.push_back(name);
            };

            for (const auto& item : stmt.items)    add(item.col);
            for (const auto& cond : stmt.where) {
                if (auto* r = std::get_if<ColumnRef>(&cond.left))  add(r->name);
                if (auto* r = std::get_if<ColumnRef>(&cond.right)) add(r->name);
            }
            for (const auto& col : stmt.group_by)  add(col);

            return cols;
        }

        static bool eval_condition(const Chunk& chunk, std::size_t row,
                                    const BinaryExpr& cond) {
            Value lval = eval_operand(chunk, row, cond.left);
            Value rval = eval_operand(chunk, row, cond.right);
            int   cmp  = compare_values(lval, rval);

            if (cond.op == "=")  return cmp == 0;
            if (cond.op == "!=") return cmp != 0;
            if (cond.op == "<")  return cmp <  0;
            if (cond.op == ">")  return cmp >  0;
            if (cond.op == "<=") return cmp <= 0;
            if (cond.op == ">=") return cmp >= 0;
            return false;
        }

        static Value eval_operand(const Chunk& chunk, std::size_t row,
                                   const std::variant<ColumnRef, Literal>& operand) {
            return std::visit([&](const auto& o) -> Value {
                using T = std::decay_t<decltype(o)>;
                if constexpr (std::is_same_v<T, ColumnRef>)
                    return chunk.get_column(o.name).get(row);
                else
                    return o.val;
            }, operand);
        }

        static int compare_values(const Value& a, const Value& b) {
            auto to_double = [](const Value& v) -> double {
                if (auto* i = std::get_if<int64_t>(&v)) return static_cast<double>(*i);
                if (auto* d = std::get_if<double>(&v))  return *d;
                return 0.0;
            };

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
    };
}
