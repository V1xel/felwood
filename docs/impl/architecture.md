# Felwood Architecture

Felwood is a single-node columnar OLAP execution engine written in C++20.

## Query Pipeline

The operator tree (bottom to top in the Volcano pull model):

```
AggregateOperator  — GROUP BY, SUM/COUNT/AVG
      ^
FilterOperator     — WHERE predicate
      ^
ScanOperator       — column projection from Table
      ^
    Table          — columnar in-memory store
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
├── docs/
│   ├── impl/       — what Felwood does
│   └── concepts/   — database theory and reference
└── src/
    ├── main.cpp
    ├── common/
    │   ├── types.hpp          — DataType enum, Value variant
    │   └── column.hpp         — Column, Chunk, BATCH_SIZE
    ├── storage/
    │   ├── table.hpp          — Schema, Table
    │   ├── catalog.hpp        — Catalog, owns all Tables
    │   └── segment_manager.hpp — persistence: read/write segment files
    ├── operators/
    │   ├── operator.hpp       — abstract Operator base
    │   ├── scan.hpp           — ScanOperator
    │   ├── filter.hpp         — FilterOperator
    │   └── aggregate.hpp      — AggregateOperator
    ├── sql/
    │   ├── lexer.hpp          — tokeniser
    │   ├── parser.hpp         — recursive descent parser
    │   ├── ast.hpp            — statement AST nodes
    │   └── planner.hpp        — builds operator tree from AST
    └── server/
        ├── mysql_proto.hpp    — MySQL wire protocol helpers
        └── mysql_server.hpp   — TCP accept loop and query dispatcher
```

All `#include` paths are relative to `src/` (set via `target_include_directories PRIVATE src` in CMakeLists.txt).
