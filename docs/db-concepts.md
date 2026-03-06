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

## Segment Files

A segment file is the on-disk unit of columnar storage used by engines like Apache Doris. Each segment holds the data for a batch of rows across all columns, plus the metadata needed to query it efficiently.

A segment contains:

- **Column data** — raw bytes for each column, compressed and encoded
- **ZoneMap index** — min/max values per page; allows skipping entire pages that cannot satisfy a WHERE condition
- **Bloom filter** — probabilistic structure per column; fast rejection of pages that don't contain a queried value
- **Bitmap index** — maps each distinct value to the set of rows containing it; efficient for low-cardinality columns
- **Delete markers / MVCC** — records which rows have been deleted or superseded by updates, without rewriting the segment
- **Transactional metadata** — commit information so partial writes are never visible to readers

Table schemas are also persisted to disk so they survive process restart.

Felwood has no segment files — all data lives in `std::vector` in RAM and is lost on process exit.
