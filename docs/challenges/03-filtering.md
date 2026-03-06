# Challenge 03 — Filtering

## Goal
Test WHERE clause behaviour across types, operators, and selectivity levels.

## Setup

```sql
CREATE TABLE orders (id INT64, customer STRING, amount FLOAT64, shipped BOOLEAN);
```

## Challenges

### 3.1 — High selectivity (few rows pass)
```sql
SELECT id, amount FROM orders WHERE amount > 9900;
```
With 10 000 rows and amounts 1–10 000, only ~100 rows pass.

**Watch for:** Full table scan regardless — no zone map to skip chunks.

### 3.2 — Low selectivity (most rows pass)
```sql
SELECT id, amount FROM orders WHERE amount > 10;
```
Almost all rows pass. Measures compaction cost of copying surviving rows.

### 3.3 — String equality
```sql
SELECT id, amount FROM orders WHERE customer = 'alice';
```
**Watch for:** Linear scan through all string values — no index.

### 3.4 — Chained AND conditions
```sql
SELECT id FROM orders WHERE amount > 100 AND amount < 500;
```
**Expected:** Both conditions evaluated per row; only rows in range returned.

### 3.5 — Filter + aggregate
```sql
SELECT customer, COUNT(amount) AS cnt FROM orders WHERE shipped = 1 GROUP BY customer;
```

## What this reveals
- Every query reads every row regardless of the WHERE predicate (no zone map, no bloom filter)
- String comparison is O(n) scan
- No short-circuit pushdown to scan layer
