# Operators

Defined in `src/operators/`.

## Operator (base class)

`src/operators/operator.hpp`

Abstract base for all pipeline operators. Implements the classic Volcano (iterator) model introduced by Graefe (1994).

```cpp
class Operator {
public:
    virtual void open() = 0;
    virtual std::optional<Chunk> next() = 0;
    virtual void close() = 0;
};
```

---

## ScanOperator

`src/operators/scan.hpp`

Reads a `Table` in slices of up to `BATCH_SIZE` rows and emits one `Chunk` per slice. Column projection is applied eagerly: only the requested columns are copied into the output chunk, so downstream operators never see — or pay for — unrequested columns.

**Constructor:**

```cpp
ScanOperator(const Table& table, std::vector<std::string> project_cols = {})
```

- `project_cols` — subset of column names to emit; empty means all columns.

**Behaviour:**

- `open()` resets `current_row_` to 0 and resolves `project_cols` to column indices via `Table::schema`.
- `next()` copies rows `[current_row_, current_row_ + BATCH_SIZE)` for each projected column; advances `current_row_`; returns `nullopt` when the table is exhausted.
- `close()` resets state.

**Limitations and future work:**

- Should read from disk via async I/O (io_uring on Linux) or memory-mapped files, overlapping I/O and CPU work.
- Columnar formats (Parquet, ORC) store pages in compressed form; the scan would decompress only the needed pages with SIMD routines.
- Page-level statistics (min/max zone maps, Bloom filters) allow skipping pages without decompressing them (predicate pushdown).
- Late materialisation: inexpensive filter columns are decoded first; expensive string columns only for rows that survive the filter.
- For partitioned tables, parallelise across worker threads, each processing an independent range of row groups.

---

## FilterOperator

`src/operators/filter.hpp`

For each input `Chunk`, evaluates a `Predicate` for every row to build a selection vector (indices of passing rows), then compacts the chunk by copying only surviving rows. Empty chunks (where no rows pass) are silently skipped; the operator loops to the next input chunk automatically, so callers never receive an empty `Chunk`.

**Types:**

```cpp
using Predicate = std::function<bool(const Chunk& chunk, std::size_t row_idx)>;
```

**Constructor:**

```cpp
FilterOperator(std::unique_ptr<Operator> child, Predicate pred)
```

**Algorithm:**

1. Pull a chunk from child.
2. Evaluate `pred` for each row; collect passing indices in `sel`.
3. If `sel` is empty, go back to step 1.
4. Compact: for each source column, copy only the rows in `sel` into a new column.
5. Return the compacted chunk.

**Limitations and future work:**

- The predicate should be compiled to a SIMD kernel (AVX-512 comparisons produce 64-bit bitmasks in a single instruction, testing all 64 rows at once).
- Selection vectors should be represented as packed bitmaps rather than arrays of indices; downstream operators can consume bitmaps directly without materialisation.
- In a JIT-compiled engine, the filter expression is inlined into the scan loop, eliminating this operator class entirely.
- Bloom filters and zone-map checks should be pushed down to the scan layer, reducing the rows that even reach the filter.
- Replace `std::function` with a typed `ExprNode` AST that can be compiled to SIMD or LLVM IR.

---

## AggregateOperator

`src/operators/aggregate.hpp`

Hash aggregation with a single STRING GROUP BY column. This is a **pipeline breaker**: it consumes all input before emitting any output.

### Supporting types

**`AggFunc`** — aggregate function selector:

| Value | Output type | Description |
|-------|-------------|-------------|
| `SUM` | FLOAT64 | Sum of values |
| `COUNT` | INT64 | Count of rows |
| `MIN` | FLOAT64 | Minimum value |
| `MAX` | FLOAT64 | Maximum value |
| `AVG` | FLOAT64 | Arithmetic mean |

**`AggSpec`** — specification for one aggregate:

| Field | Type | Description |
|-------|------|-------------|
| `input_col` | `string` | Source column name |
| `func` | `AggFunc` | Function to apply |
| `output_col` | `string` | Name in the output chunk |

**`AggState`** — per-group, per-aggregate accumulator. All values are promoted to `double`. Tracks running `sum`, `count`, `min_val`, and `max_val` for efficient finalisation of any function without separate passes.

### Algorithm

**Phase 1 — build hash table (`build_hash_table` / `accumulate`):**

- Drain all chunks from the child operator.
- For each chunk, resolve column references once per chunk (cached as raw pointers).
- For each row, `try_emplace` the group key into `hash_table_`; record first-seen order in `group_order_` for deterministic output.
- Update the `AggState` vector for that group.

**Phase 2 — materialise result (`produce_result`):**

- Emit one output column for the GROUP BY key (STRING).
- Emit one output column per `AggSpec`, in `group_order_` order.
- COUNT produces INT64; all others produce FLOAT64.

**Constructor:**

```cpp
AggregateOperator(std::unique_ptr<Operator> child,
                  std::string group_by_col,
                  std::vector<AggSpec> agg_specs)
```

**Constraints:**

- GROUP BY column must be STRING.
- Aggregate input columns must be INT64 or FLOAT64.
- Only one GROUP BY column is supported.

**Limitations and future work:**

- Multi-column GROUP BY: composite key (hash of N columns) with xxHash or HighwayHash.
- Two-phase aggregation for parallelism: each thread maintains a local hash table; a global merge phase combines them.
- Replace `std::unordered_map` with open-addressing (Robin Hood / Swiss-table) for better cache utilisation.
- SIMD-accelerated hashing: process 4–8 rows per cycle with AVX2.
- Spill-to-disk when the hash table exceeds a memory budget (Grace hash aggregation).
- Separate typed accumulators to avoid precision loss (e.g., keep SUM as `int64_t` for integer columns).
- Handle NULL inputs per SQL semantics (`COUNT(*)` vs `COUNT(col)`).
