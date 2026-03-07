#pragma once

#include "common/types.hpp"
#include "storage/table.hpp"

#include <string>
#include <vector>
#include <optional>
#include <variant>

namespace felwood {
    struct ColumnRef { std::string name; };
    struct Literal   { Value val; };

    struct BinaryExpr {
        std::string                      op;
        std::variant<ColumnRef, Literal> left;
        std::variant<ColumnRef, Literal> right;
    };

    struct SelectItem {
        std::optional<AggFunc> agg;
        std::string            col;
        std::string            alias;
    };

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
        std::vector<BinaryExpr>  where;
        std::vector<std::string> group_by;
        bool                     star = false;
    };

    using Stmt = std::variant<CreateTableStmt, InsertStmt, SelectStmt>;
}
