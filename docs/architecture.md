# Felwood Architecture

Felwood is a single-node, in-memory columnar OLAP execution engine written in C++20.

## Query Pipeline

The engine executes the following hardcoded demo query against an in-memory `employees` table:

```sql
SELECT department,
       SUM(salary)   AS sum_salary,
       COUNT(salary)  AS count_salary,
       AVG(salary)   AS avg_salary
FROM   employees
WHERE  salary > 50000
GROUP  BY department
```

The operator tree (bottom to top in the Volcano pull model):

```
AggregateOperator  — GROUP BY department, SUM/COUNT/AVG salary
      ^
FilterOperator     — salary > 50 000
      ^
ScanOperator       — projects {department, salary} from Table
      ^
    Table          — in-memory columnar store (employees)
```

## Execution Model: Volcano / Iterator

Every operator implements three calls:

| Call | Description |
|------|-------------|
| `open()` | Initialise internal state; recurse to child operators |
| `next()` | Return the next output `Chunk`; `std::nullopt` signals exhaustion |
| `close()` | Release resources; recurse to child operators |

Callers drive the pipeline:

```cpp
op->open();
while (auto chunk = op->next()) { /* consume */ }
op->close();
```

Data flows upward in batches of up to `BATCH_SIZE` (1 024) rows. The `AggregateOperator` is a pipeline breaker: it consumes all input before emitting any output.

## Source Layout

```
felwood/
├── CMakeLists.txt
├── docs/               — this documentation
└── src/
    ├── main.cpp        — entry point, builds table, starts server
    ├── common/
    │   ├── types.hpp   — DataType enum, Value variant
    │   └── column.hpp  — Column, Chunk, BATCH_SIZE
    ├── storage/
    │   └── table.hpp   — Schema, Table
    ├── operators/
    │   ├── operator.hpp   — abstract Operator base
    │   ├── scan.hpp       — ScanOperator
    │   ├── filter.hpp     — FilterOperator
    │   └── aggregate.hpp  — AggregateOperator
    └── server/
        ├── mysql_proto.hpp  — MySQL wire protocol helpers
        └── mysql_server.hpp — TCP accept loop and query dispatcher
```

All `#include` paths are relative to `src/` (set via `target_include_directories PRIVATE src` in CMakeLists.txt).

## In a Real Engine

A production query engine would add:

- **Parser** (Bison / ANTLR) to convert SQL text into an AST
- **Binder** to resolve names against the catalog
- **Logical planner** to build a relational algebra tree
- **Rule-based optimizer** for predicate pushdown and column pruning
- **Cost-based optimizer** for cardinality estimation and join reordering
- **Physical plan builder** that emits operator instances
- **Push / producer model** (Hyper, Velox) to avoid virtual calls in the hot loop
- **Morsel-driven parallelism** (Leis et al. 2014) for multi-threaded execution
- **JIT / codegen** (LLVM) to fuse operators into a single tight loop per pipeline
