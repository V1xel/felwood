# Table

Defined in `src/common/column.hpp` and `src/storage/table.hpp`.

## BATCH_SIZE

```cpp
inline constexpr std::size_t BATCH_SIZE = 1024;
```

Controls how many rows flow through the pipeline in a single `Chunk`. At 1 024 rows × 8 bytes per element, two INT64 columns fit inside a typical 16 KiB L1 cache.

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

> Note: `std::vector<bool>` is bit-packed by the standard, which differs from the contiguous byte layout that SIMD operations expect.

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

Owns the ground-truth data. Stores columns side-by-side (column-major layout) so a scan reads only the columns it needs.

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
