#pragma once

#include "common/types.hpp"
#include "storage/table.hpp"

#include <string>
#include <vector>
#include <optional>
#include <variant>

namespace felwood {

// ── Expressions (used in WHERE) ───────────────────────────────────────────────

struct ColumnRef { std::string name; };
struct Literal   { Value val; };

struct BinaryExpr {
    std::string                      op;    // "=", "!=", "<", ">", "<=", ">="
    std::variant<ColumnRef, Literal> left;
    std::variant<ColumnRef, Literal> right;
};

// ── SELECT list item ──────────────────────────────────────────────────────────

struct SelectItem {
    std::optional<AggFunc> agg;    // nullopt = plain column
    std::string            col;    // source column name
    std::string            alias;  // output name (same as col if no AS)
};

// ── Statements ────────────────────────────────────────────────────────────────

struct CreateTableStmt {
    std::string               name;
    std::vector<ColumnSchema> cols;
};

struct InsertStmt {
    std::string        table;
    std::vector<Value> values;
};

struct SelectStmt {
    std::vector<SelectItem>  items;
    std::string              from;
    std::vector<BinaryExpr>  where;     // AND-connected conditions
    std::vector<std::string> group_by;
};

using Stmt = std::variant<CreateTableStmt, InsertStmt, SelectStmt>;

} // namespace felwood
