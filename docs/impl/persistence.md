# Persistence

Implemented in `src/storage/segment_manager.hpp` and `src/storage/catalog.hpp`.

## File Layout

Each table is stored as a single segment file:

```
felwood_data/<table_name>/segment.seg
```

Structure (top to bottom):

```
[magic — 4 bytes]                    0x464C5744 "FLWD"
[column 0 data]
[column 1 data]
...
[column N data]
[footer]
  uint64_t  num_rows
  uint32_t  num_columns
  per column:
    uint8_t   name_length
    char[]    name (name_length bytes)
    uint8_t   type (DataType ordinal)
    uint64_t  offset (byte offset from start of file)
    uint64_t  size   (byte count)
[uint32_t  footer_size]
[magic — 4 bytes]                    closing marker
```

## Column Encoding

| Type | Encoding |
|------|----------|
| INT64 / FLOAT64 | Raw 8-byte little-endian value per row |
| BOOLEAN | 1 byte per row (0 = false, 1 = true) |
| STRING | `[uint32_t len][len bytes]` per row |

No compression is applied yet.

## Read Path

The footer is the entry point — the reader never scans from byte 0:

1. Seek to `end - 4` → read and verify closing magic
2. Seek to `end - 8` → read `footer_size`
3. Seek to `end - 8 - footer_size` → read footer (get column offsets)
4. For each needed column: seek directly to its `offset`, read `size` bytes
5. Columns not needed are never touched

## Write Path

`SegmentManager::flush(table)` rewrites the complete segment file from the current in-memory `Table` state. Called after every `CREATE TABLE` and `INSERT`.

`Catalog` holds all tables in memory (`std::vector` inside each `Column`). The segment file is always a full serialisation of that in-memory state.

## On-Disk Example

Given:

```sql
INSERT INTO orders VALUES (1, 'alice', 99.5);
INSERT INTO orders VALUES (2, 'bob',   42.0);
```

`orders/segment.seg` (Felwood):

```
46 4C 57 44                          ← magic "FLWD"

── id column ──
01 00 00 00 00 00 00 00              ← int64 1
02 00 00 00 00 00 00 00              ← int64 2

── name column ──
05 00 00 00 61 6C 69 63 65           ← uint32 len=5, "alice"
03 00 00 00 62 6F 62                 ← uint32 len=3, "bob"

── price column ──
00 00 00 00 00 00 49 40              ← double 99.5 (IEEE 754)
00 00 00 00 00 00 45 40              ← double 42.0

── footer ──
02 00 00 00 00 00 00 00              ← num_rows = 2
03 00 00 00                          ← num_cols = 3
02 69 64 00 04 00 00 ...             ← col: len=2 "id" type=INT64 offset size
...

[footer_size — 4 bytes]
46 4C 57 44                          ← closing magic
```
