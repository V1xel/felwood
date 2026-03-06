# Storage Layer

Defined in `src/common/column.hpp` and `src/storage/table.hpp`.

## BATCH_SIZE

```cpp
inline constexpr std::size_t BATCH_SIZE = 1024;
```

Controls how many rows flow through the pipeline in a single `Chunk`. At 1 024 rows × 8 bytes per element, two INT64 columns fit inside a typical 16 KiB L1 cache. Tuning this constant to the target CPU's cache hierarchy is a meaningful performance lever.

## ColumnData

Physical storage for one column's values, typed via `std::variant`:

```cpp
using ColumnData = std::variant<
    std::vector<int64_t>,
    std::vector<double>,
    std::vector<std::string>,
    std::vector<bool>
>;
```

The variant index matches the `DataType` ordinal (same ordering as in `types.hpp`).

> Note: `std::vector<bool>` is bit-packed by the standard, which differs from the contiguous byte layout that SIMD operations expect. A real engine would use `std::vector<uint8_t>` or a custom bitmap.

## Column

A named, typed, single-column buffer.

| Member | Type | Description |
|--------|------|-------------|
| `name` | `std::string` | Column name |
| `type` | `DataType` | Physical type tag |
| `data` | `ColumnData` | Typed vector of values |

Key methods:

| Method | Description |
|--------|-------------|
| `size()` | Number of stored elements |
| `append(val)` | Append one `Value`; throws on type mismatch |
| `get(idx)` | Random-access read; returns a `Value` |

## Chunk

The unit of data that flows between operators in the Volcano pipeline.

| Member | Type | Description |
|--------|------|-------------|
| `columns` | `vector<Column>` | Ordered set of columns |
| `num_rows` | `size_t` | Row count for this batch |

Key methods:

| Method | Description |
|--------|-------------|
| `empty()` | True if `num_rows == 0` |
| `find_column(name)` | Linear scan; returns index or `nullopt` |
| `get_column(name)` | Throws if column not found |

## Schema

Lightweight metadata: an ordered list of `(name, DataType)` pairs describing a table's columns.

## Table

Owns the ground-truth data. Stores columns side-by-side (column-major layout) so a scan reads only the columns it needs, skipping the rest entirely — the primary advantage of columnar storage.

| Member | Type | Description |
|--------|------|-------------|
| `name` | `std::string` | Table name |
| `schema` | `Schema` | Column metadata |
| `columns` | `vector<Column>` | One `Column` per schema entry, all equal length |

Key methods:

| Method | Description |
|--------|-------------|
| `num_rows()` | Row count (from the first column) |
| `num_cols()` | Column count |
| `append_row(row)` | Append one row as a parallel vector of `Value`s |
| `get_column(name/idx)` | Column access by name or index |

## Limitations and Future Work

**Column storage:**
- Buffers should be arena-allocated and cache-line-aligned instead of using `std::vector`, making memory layout predictable and SIMD-friendly.
- NULL tracking requires a separate validity bitmap (one bit per row), not a sentinel value.
- Strings should use the Arrow BinaryArray layout: offsets + lengths into a shared byte buffer, avoiding per-string heap allocations.
- Dictionary encoding (small integer index + dictionary vector) reduces memory for low-cardinality string columns by 10–100×.

**Table / storage layer:**
- Data should be stored on disk as compressed column groups (Parquet, ORC, or custom binary) with a Buffer Manager handling page eviction and memory-mapping.
- Per-page statistics (min, max, null count, Bloom filter) enable partition/page pruning before data is read.
- A Catalog (backed by SQLite or a KV store) should map table names to file paths, schemas, and statistics.
- For distributed engines: the catalog also tracks data partitioning (hash, range) and replica placement.
- Mutable tables require a delta-store + immutable base (LSM-tree style).
