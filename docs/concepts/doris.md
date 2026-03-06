# Apache Doris Storage Architecture

## Segment Files

A segment file is the on-disk unit of columnar storage. Each segment holds the data for a batch of rows across all columns, plus the metadata needed to query it efficiently.

A segment contains:

- **Column data** — raw bytes for each column, compressed and encoded
- **ZoneMap index** — min/max values per page; allows skipping entire pages that cannot satisfy a WHERE condition
- **Bloom filter** — probabilistic structure per column; fast rejection of pages that don't contain a queried value
- **Bitmap index** — maps each distinct value to the set of rows containing it; efficient for low-cardinality columns
- **Delete markers / MVCC** — records which rows have been deleted or superseded by updates, without rewriting the segment
- **Transactional metadata** — commit information so partial writes are never visible to readers
- **Footer** — protobuf metadata at end of file containing all offsets; the reader's entry point (same pattern as Parquet/ORC)

## File Structure (annotated)

```
segment_0.dat
│
├── [Magic bytes]  4 bytes — file signature
│
├── ── Column: id ──────────────────────────────────────────────
│   ├── Page 0
│   │   ├── Header: page type, uncompressed size, compressed size
│   │   └── Body: LZ4/Zstd compressed column values
│   │       (delta / RLE / dictionary / plain encoding first)
│   ├── Page 1 ...  (next 64KB chunk of rows)
│   ├── Ordinal Index   (row number → page offset in file)
│   ├── Zone Map        (per page: min, max, has_null)
│   └── Bloom Filter    (per page: bitset for membership test)
│
├── ── Column: name ────────────────────────────────────────────
│   └── (same structure)
│
├── ── Column: price ───────────────────────────────────────────
│   └── (same structure)
│
├── ── Short Key Index ─────────────────────────────────────────
│   Sparse index on sort-key prefix columns.
│   Maps key prefix → row number for fast range scan entry.
│
├── ── Footer (protobuf) ───────────────────────────────────────
│   ├── num_rows, num_columns
│   ├── schema (column names, types, encoding, compression)
│   └── per column: ordinal index offset, zone map offset,
│                   bloom filter offset, page offsets
│
├── [Footer length]  4 bytes
└── [Magic bytes]    4 bytes — end marker; reader starts here
```

## Read Path

```
open file
  → read last 4 bytes   (verify magic)
  → read next 4 bytes   (footer length)
  → read footer         (know where everything is)
  → check zone map      (skip pages outside predicate range)
  → check bloom filter  (skip pages that can't contain the value)
  → decompress page     (only pages that survive the above)
  → decode values       (undo delta/RLE/dictionary)
  → return rows
```

The footer is the entry point — the reader never scans from byte 0. This makes it practical to store all columns in one file while still seeking directly to any column without reading the others.

## Column Encoding

Before LZ4/Zstd compression, values are encoded to make the raw bytes smaller:

| Data pattern | Encoding |
|---|---|
| Sorted integers | Delta encoding — `[100,101,102]` → `[100, +1, +1]` |
| Repeated values | Run-length encoding — `[5,5,5,5]` → `[(5, 4)]` |
| Low cardinality | Dictionary encoding — `["alice","alice","bob"]` → `[0,0,1]` + dict |
| General | Plain (raw bytes) — fallback |

## File Hierarchy

One segment file per flush batch — a busy table can accumulate many:

```
tablet/
├── rowset_0/          ← first MemTable flush
│   ├── segment_0.dat
│   └── segment_1.dat  ← spilled over 256MB limit
├── rowset_1/          ← second flush
│   └── segment_0.dat
└── rowset_2/          ← after compaction merges rowset_0 + rowset_1
    └── segment_0.dat
```

Compaction merges Rowsets into fewer files, not necessarily one:

**Size-tiered** (Doris default):
```
[1MB] [1MB] [1MB] [1MB]  →  [4MB]
[4MB] [4MB] [4MB] [4MB]  →  [16MB]
```

**Leveled** (RocksDB style):
```
Level 0:  [seg_a] [seg_b] [seg_c]   ← fresh flushes, may overlap
Level 1:  [seg_1–10] [seg_11–20]    ← merged, sorted, no overlap
Level 2:  [seg_1–100]
```

The file count never reaches one in practice because segments are capped at 256MB and new flushes keep arriving while compaction runs in the background.

## Felwood vs Doris

| Concern | Felwood | Doris |
|---|---|---|
| Crash safety | None — mid-flush = lost row | WAL replayed on restart |
| Write path | Rewrite full segment per INSERT | MemTable → sorted immutable Rowset |
| File structure | 1 segment file per table (grows unbounded) | Many segment files per tablet, compacted |
| Indexes | None | Zone map, bloom filter, bitmap per segment |
| Read skipping | Full scan always | Skip segments/pages via zone map |
| Key semantics | Always append | Duplicate / Aggregate / Unique at compaction |
| Compression | None | LZ4 / Zstd per column block |
| Encoding | Raw bytes | Delta / RLE / dictionary before compression |

## On-Disk Format: Side by Side

Given:
```sql
INSERT INTO orders VALUES (1, 'alice', 99.5);
INSERT INTO orders VALUES (2, 'bob',   42.0);
```

**Felwood** — one segment file, raw bytes, no indexes:
```
46 4C 57 44                             ← magic "FLWD"
01 00 00 00 00 00 00 00                 ← id=1 (int64)
02 00 00 00 00 00 00 00                 ← id=2
05 00 00 00 61 6C 69 63 65             ← "alice" (len-prefixed)
03 00 00 00 62 6F 62                   ← "bob"
00 00 00 00 00 00 49 40                ← price=99.5 (IEEE 754)
00 00 00 00 00 00 45 40                ← price=42.0
[footer: offsets to each column]
[footer_size][magic]
```

**Doris** — one segment file, compressed, with indexes:
```
[magic]
[id column — LZ4([1,2])]
  zone map: min=1 max=2
  bloom filter: {1,2}
[name column — LZ4(["alice","bob"])]
  zone map: min="alice" max="bob"
  bloom filter: {"alice","bob"}
[price column — LZ4([99.5,42.0])]
  zone map: min=42.0 max=99.5
[short key index]
[footer: all offsets, schema, compression info]
[footer_size][magic]
```
