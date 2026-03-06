# Challenge 02 — Aggregation

## Goal
Test GROUP BY and aggregate functions across varying data sizes and cardinalities.

## Setup

```sql
CREATE TABLE sales (region STRING, product STRING, amount FLOAT64, quantity INT64);
```

Insert rows with a small set of regions: `north`, `south`, `east`, `west`.

## Challenges

### 2.1 — Basic SUM
```sql
SELECT region, SUM(amount) AS total FROM sales GROUP BY region;
```
**Expected:** One row per region, correct sums.

### 2.2 — Multiple aggregates
```sql
SELECT region, SUM(amount) AS total, COUNT(amount) AS cnt, AVG(amount) AS avg, MIN(amount) AS lo, MAX(amount) AS hi FROM sales GROUP BY region;
```
**Expected:** All five aggregates correct in one pass.

### 2.3 — High cardinality GROUP BY
Insert 10 000 rows each with a unique region string (e.g. `region_0001` … `region_9999`).

```sql
SELECT region, SUM(amount) AS total FROM sales GROUP BY region;
```

**Watch for:**
- Hash table grows to 10 000 entries in RAM
- Output ordering — Felwood preserves insertion order, not sorted

### 2.4 — Aggregation after filter
```sql
SELECT region, SUM(amount) AS total FROM sales WHERE amount > 500 GROUP BY region;
```
**Expected:** Only rows passing the filter are aggregated.

## What this reveals
- Hash table memory usage at high cardinality
- No spill-to-disk when hash table exceeds RAM
- Output is insertion-ordered, not sorted (no ORDER BY yet)
