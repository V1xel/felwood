# Persistence

## Felwood vs Doris

Felwood's `StorageManager` is a minimal append-only column file writer. Doris does everything on top of that.

| Concern | Felwood | Doris |
|---|---|---|
| Crash safety | None — mid-flush = lost row | WAL replayed on restart |
| Write path | Direct file append per row | MemTable → sorted immutable Rowset |
| File structure | 1 file per column, forever growing | Segment files (columns + indexes, ≤256MB, then new segment) |
| Indexes | None | Zone map, bloom filter, bitmap per segment |
| Read skipping | Full scan always | Skip entire segments/pages via zone map |
| Merging | Never | Compaction merges segments in background |
| Key semantics | Always append | Duplicate / Aggregate / Unique at compaction |
| Compression | None | LZ4 / Zstd per column block |

The biggest practical gaps are **no WAL** (data loss on crash) and **no indexes** (every query reads every row regardless of filters).

---

## On-Disk Format Comparison

Given:

```sql
INSERT INTO orders VALUES (1, 'alice', 99.5);
INSERT INTO orders VALUES (2, 'bob',   42.0);
```

### Felwood — one binary file per column

`orders/id.col`
```
01 00 00 00 00 00 00 00   ← int64 1
02 00 00 00 00 00 00 00   ← int64 2
```

`orders/name.col`
```
05 00 00 00 61 6C 69 63 65   ← uint32 len=5, "alice"
03 00 00 00 62 6F 62         ← uint32 len=3, "bob"
```

`orders/price.col`
```
00 00 00 00 00 00 49 40   ← double 99.5 (IEEE 754)
00 00 00 00 00 00 45 40   ← double 42.0
```

No header, no row count, no indexes. Reading a single row requires scanning all three files in full.

### Doris — one self-contained segment file

```
┌─────────────────────────────────────────────┐
│  Column: id                                 │
│    page 0: [1, 2]  (LZ4 compressed)         │
│    zone map: min=1 max=2                     │
│    bloom filter: {1, 2}                      │
├─────────────────────────────────────────────┤
│  Column: name                               │
│    page 0: ["alice", "bob"]  (LZ4)          │
│    zone map: min="alice" max="bob"           │
│    bloom filter: {"alice", "bob"}            │
├─────────────────────────────────────────────┤
│  Column: price                              │
│    page 0: [99.5, 42.0]  (LZ4)              │
│    zone map: min=42.0 max=99.5              │
├─────────────────────────────────────────────┤
│  Short key index: [(row 0 → page offset)]   │
│  Ordinal index:   [(row 0 → byte 0), ...]   │
├─────────────────────────────────────────────┤
│  Footer (protobuf): column count, types,    │
│    page offsets, index offsets, row count   │
│  Footer length (4 bytes)                    │
│  Magic bytes                                │
└─────────────────────────────────────────────┘
```

A query `WHERE id = 2` hits the bloom filter → confirms the page contains it → decompresses only that page. With Felwood, every byte of every file is read unconditionally.

---

## Doris File Count & Compaction

Doris does not store everything in one file. Each MemTable flush produces a new **Rowset**, and each Rowset contains one or more Segment files (split at 256MB). A busy table can accumulate hundreds of segment files over time.

```
tablet/
├── rowset_0/          ← first MemTable flush
│   ├── segment_0.dat
│   └── segment_1.dat  ← spilled over 256MB
├── rowset_1/          ← second flush
│   └── segment_0.dat
└── rowset_2/          ← after compaction merges rowset_0 + rowset_1
    └── segment_0.dat  ← larger, fewer files
```

Compaction merges Rowsets into **fewer** files, not necessarily one. The result depends on the strategy:

**Size-tiered** (Doris default) — merge files of similar size together:
```
[1MB] [1MB] [1MB] [1MB]  →  [4MB]
[4MB] [4MB] [4MB] [4MB]  →  [16MB]
```
Write-efficient but ends up with multiple large files.

**Leveled** (RocksDB style) — files are organised into levels with size limits:
```
Level 0:  [seg_a] [seg_b] [seg_c]   ← fresh flushes, may overlap
Level 1:  [seg_1–10] [seg_11–20]    ← merged, sorted, no overlap
Level 2:  [seg_1–100]               ← larger merged ranges
```
Read-efficient (one file per level per query) but more write I/O.

The file count never reaches one in practice because:
- Segments are capped at 256MB — large tables always span many files
- New flushes keep arriving while compaction runs in the background
- Compaction runs continuously, not to a final state

The goal is keeping the count low enough that a query opens a **handful** of files rather than thousands.
