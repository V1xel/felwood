# Database Engineering Concepts

## Arena Allocation

Replace many `malloc`/`free` calls with one large upfront allocation. Subsequent allocations just bump a pointer forward. Free the entire arena at once when done (e.g., end of query batch).

Benefits: fast allocation, no fragmentation, cache-friendly (allocated objects are physically close in memory).

Felwood uses `std::vector` instead — implicit heap allocation per column buffer.

## Buffer Pool

A fixed pool of equal-sized memory pages (e.g., 8KB each). Column data on disk is split into matching page-sized chunks. When a page is needed, the pool loads it from disk. When the pool is full, the least recently used page is evicted.

Callers request pages by `(file, page_number)` and get back a memory pointer — disk vs memory is invisible to the operator layer.

Felwood has no buffer pool — all data lives in `std::vector` in RAM. Only works while the dataset fits in memory.

## Spill-to-Disk

When a data structure (e.g., a hash table in `AggregateOperator`) grows beyond the memory budget, the engine explicitly writes part of it to disk to free RAM, then reads it back when needed.

The common algorithm is **Grace Hash Aggregation**: hash the group key into N partitions, write each partition to disk, then load and process one partition at a time — each small enough to fit in RAM.

The OS has its own swap mechanism that moves memory to disk transparently, but databases don't rely on it — it's coarse (4KB pages), blind to data structure semantics, and reactive rather than proactive. Explicit spilling gives the engine full control over what is evicted, when, and in what format.

Felwood's `AggregateOperator` has no memory budget or spilling — if the hash table exceeds available RAM the process runs out of memory.

## Write-Ahead Log (WAL)

Before modifying any in-memory or on-disk data structure, the change is appended to a WAL file first (sequential I/O — fast). On crash, the engine replays the WAL to reconstruct any writes that hadn't been fully flushed to their final storage location. The WAL is truncated once the corresponding data has been durably flushed.

Felwood has no WAL — if the process dies mid-flush, any rows being written at that moment are silently lost.

## Tablet

A tablet is a horizontal partition of a table — a subset of rows assigned by hashing a key column. When a table is created, it is split into N tablets upfront; each tablet is stored on one node and replicated across others for fault tolerance.

Tablets are the smallest unit of data movement: when a node is added or removed, the cluster migrates whole tablets, not individual rows.

Each tablet manages its own rowsets and segments independently — compaction runs per-tablet.

Felwood has no tablets — it is single-node so the entire table is one implicit tablet.

## Rowset

A rowset is the immutable on-disk result of one MemTable flush. It contains one or more segment files (each ≤256MB). Once written, a rowset is never modified — new writes produce new rowsets, and compaction merges old ones.

Rowsets accumulate until compaction merges them into fewer, larger rowsets. A query may need to read multiple rowsets and merge the results.

Felwood has no rowsets — a single segment file per table is rewritten in place on every INSERT.

## MemTable

An in-memory write buffer that accumulates incoming rows before they are flushed to disk as an immutable file. In engines like Apache Doris or RocksDB, the MemTable is sorted by the primary/sort key, so each flush produces a sorted run on disk.

Benefits: amortises the cost of many small writes into one large sequential write, and the sort enables efficient merge during compaction.

Felwood has no MemTable — each INSERT rewrites the segment file immediately.

## Compaction

The background process that merges multiple small immutable files (produced by MemTable flushes) into fewer, larger files. This reduces **read amplification** — the number of files that must be scanned per query — at the cost of extra write I/O.

Compaction is what makes LSM-tree engines (RocksDB, Doris, ClickHouse MergeTree) efficient for reads despite using an append-only write path.

Two common strategies:
- **Size-tiered** — merge files of similar size; write-efficient but produces large files
- **Leveled** — files are organised into levels with size limits; read-efficient but more write I/O

Felwood has no compaction — a single segment file per table is rewritten on every INSERT.

## Stable Identifiers (ID-based Storage Layout)

Database objects (tables, databases) are stored on disk using numeric IDs rather than their user-facing names. The name-to-ID mapping lives in metadata (e.g., Doris FE catalog, PostgreSQL pg_class).

This means a rename (`ALTER TABLE RENAME`) is a cheap metadata update — only the mapping changes, nothing on disk moves. If folders were named after tables, renaming would require moving gigabytes of data across all nodes.

The same principle appears in filesystems: filenames are just directory entries that point to inodes (numeric IDs). The actual data is addressed by inode, not by name.

Felwood uses folder names directly (`felwood_data/orders/`) — renaming a table would require renaming the folder and rewriting the segment.

## Key Models (Table Semantics)

How an engine handles rows with the same primary key:

- **Duplicate Key** — all rows are kept exactly as inserted (pure append)
- **Aggregate Key** — rows sharing the same key are merged at compaction time using a specified aggregate function (SUM, MAX, etc.); useful for pre-aggregated fact tables
- **Unique Key** — only the latest version of each key is retained (upsert semantics); older versions are discarded at compaction

Apache Doris exposes all three as a table-creation option. ClickHouse achieves similar results via `ReplacingMergeTree` (Unique) and `AggregatingMergeTree` (Aggregate).

Felwood uses Duplicate Key only — every INSERT appends a new row unconditionally.
