# Future Work & Limitations

## What Real Engines Add (Architecture)

- **Binder** to resolve names against the catalog
- **Logical planner** to build a relational algebra tree
- **Rule-based optimizer** for predicate pushdown and column pruning
- **Cost-based optimizer** for cardinality estimation and join reordering
- **Push / producer model** (Hyper, Velox) to avoid virtual calls in the hot loop
- **Morsel-driven parallelism** (Leis et al. 2014) for multi-threaded execution
- **JIT / codegen** (LLVM) to fuse operators into a single tight loop per pipeline

---

## Type System

- **NULL** is not represented. A real engine uses a separate validity bitmap (one bit per row) so that nullability does not require sentinel values or `std::optional` wrappers on every cell.
- **Additional types**: DECIMAL, DATE32, TIMESTAMP64, LIST, STRUCT are absent.
- **Physical vs logical type distinction**: e.g., a dictionary-encoded STRING column has logical type STRING but physical type INT32 (dictionary index). This separation enables DICT encoding without touching the logical layer.
- **Arrow compatibility**: aligning type IDs with Apache Arrow would allow zero-copy import of Arrow record batches.

---

## Column / Table Storage

- Buffers should be arena-allocated and cache-line-aligned instead of using `std::vector`, making memory layout predictable and SIMD-friendly.
- NULL tracking requires a separate validity bitmap (one bit per row), not a sentinel value.
- Strings should use the Arrow BinaryArray layout: offsets + lengths into a shared byte buffer, avoiding per-string heap allocations.
- Dictionary encoding (small integer index + dictionary vector) reduces memory for low-cardinality string columns by 10–100×.
- `std::vector<bool>` is bit-packed by the standard, which differs from the contiguous byte layout SIMD operations expect. Use `std::vector<uint8_t>` or a custom bitmap instead.
- Data should be stored as compressed column groups (Parquet, ORC, or custom binary) with a Buffer Manager handling page eviction and memory-mapping.
- Per-page statistics (min, max, null count, bloom filter) enable partition/page pruning before data is read.
- A Catalog backed by SQLite or a KV store should map table names to file paths, schemas, and statistics.
- For distributed engines: the catalog also tracks data partitioning (hash, range) and replica placement.
- Mutable tables require a delta-store + immutable base (LSM-tree style).

---

## ScanOperator

- Should read from disk via async I/O (io_uring on Linux) or memory-mapped files, overlapping I/O and CPU work.
- Columnar formats (Parquet, ORC) store pages in compressed form; the scan would decompress only the needed pages with SIMD routines.
- Page-level statistics (min/max zone maps, bloom filters) allow skipping pages without decompressing them (predicate pushdown).
- Late materialisation: inexpensive filter columns are decoded first; expensive string columns only for rows that survive the filter.
- For partitioned tables, parallelise across worker threads, each processing an independent range of row groups.

---

## FilterOperator

- The predicate should be compiled to a SIMD kernel (AVX-512 comparisons produce 64-bit bitmasks in a single instruction, testing all 64 rows at once).
- Selection vectors should be represented as packed bitmaps rather than arrays of indices; downstream operators can consume bitmaps directly without materialisation.
- In a JIT-compiled engine, the filter expression is inlined into the scan loop, eliminating this operator class entirely.
- Bloom filters and zone-map checks should be pushed down to the scan layer, reducing the rows that even reach the filter.
- Replace `std::function` with a typed `ExprNode` AST that can be compiled to SIMD or LLVM IR.

---

## AggregateOperator

- Multi-column GROUP BY: composite key (hash of N columns) with xxHash or HighwayHash.
- Two-phase aggregation for parallelism: each thread maintains a local hash table; a global merge phase combines them.
- Replace `std::unordered_map` with open-addressing (Robin Hood / Swiss-table) for better cache utilisation.
- SIMD-accelerated hashing: process 4–8 rows per cycle with AVX2.
- Spill-to-disk when the hash table exceeds a memory budget (Grace hash aggregation).
- Separate typed accumulators to avoid precision loss (e.g., keep SUM as `int64_t` for integer columns).
- Handle NULL inputs per SQL semantics (`COUNT(*)` vs `COUNT(col)`).

---

## Persistence

- **WAL**: write each row to a WAL file before updating the segment — enables crash recovery without changing the segment format.
- **MemTable**: accumulate N rows in memory before flushing — reduces the cost of rewriting the segment per INSERT to amortised O(1).
- **Zone map**: track min/max per column block — enables skipping entire blocks during scans without reading them.
- **Bloom filter**: probabilistic per-segment membership — fast rejection for point lookups.
- **Segment compaction**: merge multiple segment files per table — reduces read amplification as INSERT count grows.
- **Compression**: LZ4 or Zstd on column blocks — reduces I/O for large tables.
- **Key models**: Aggregate Key and Unique Key semantics at compaction time.
