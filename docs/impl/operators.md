# Operators

Defined in `src/operators/`.

## Operator (base class)

`src/operators/operator.hpp`

Abstract base for all pipeline operators. Implements the Volcano (iterator) model.

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

Reads a `Table` in slices of up to `BATCH_SIZE` rows and emits one `Chunk` per slice. Column projection is applied eagerly: only the requested columns are copied into the output chunk.

**Constructor:**

```cpp
ScanOperator(const Table& table, std::vector<std::string> project_cols = {})
```

- `project_cols` — subset of column names to emit; empty means all columns.

**Behaviour:**

- `open()` resets `current_row_` to 0 and resolves `project_cols` to column indices via `Table::schema`.
- `next()` copies rows `[current_row_, current_row_ + BATCH_SIZE)` for each projected column; advances `current_row_`; returns `nullopt` when the table is exhausted.
- `close()` resets state.

---

## FilterOperator

`src/operators/filter.hpp`

For each input `Chunk`, evaluates a `Predicate` for every row to build a selection vector (indices of passing rows), then compacts the chunk by copying only surviving rows. Empty chunks are silently skipped.

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

**`AggState`** — per-group, per-aggregate accumulator. All values are promoted to `double`. Tracks running `sum`, `count`, `min_val`, and `max_val`.

### Algorithm

**Phase 1 — build hash table:**

- Drain all chunks from the child operator.
- For each chunk, resolve column references once per chunk (cached as raw pointers).
- For each row, `try_emplace` the group key into `hash_table_`; record first-seen order in `group_order_` for deterministic output.
- Update the `AggState` vector for that group.

**Phase 2 — materialise result:**

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
