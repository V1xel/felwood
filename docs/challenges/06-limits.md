# Challenge 06 — Limits & Edge Cases

## Goal
Find where Felwood breaks or behaves unexpectedly.

## Challenges

### 6.1 — Empty table query
```sql
CREATE TABLE empty (id INT64, name STRING);
SELECT id FROM empty;
```
**Expected:** No rows returned, no crash.

### 6.2 — Single row table
```sql
CREATE TABLE singleton (x FLOAT64);
INSERT INTO singleton VALUES (3.14);
SELECT x, SUM(x) AS s, COUNT(x) AS c FROM singleton GROUP BY x;
```

### 6.3 — Duplicate table name
```sql
CREATE TABLE dup (id INT64);
CREATE TABLE dup (id INT64);
```
**Expected:** Error on second CREATE.

### 6.4 — Query non-existent table
```sql
SELECT id FROM ghost;
```
**Expected:** Error, not crash.

### 6.5 — Large string values
Insert rows with very long strings (1 KB, 10 KB per value).

**Watch for:**
- Memory usage scales with string size × num_rows
- Segment file size grows accordingly

### 6.6 — Numeric extremes
```sql
CREATE TABLE extremes (big INT64, small FLOAT64);
INSERT INTO extremes VALUES (9223372036854775807, 1.7976931348623157e+308);
INSERT INTO extremes VALUES (-9223372036854775808, 5e-324);
SELECT big, SUM(small) AS s FROM extremes GROUP BY big;
```
**Watch for:** INT64 max/min, FLOAT64 max/min — precision loss when promoted to double in aggregation.

### 6.7 — Many small tables
Create 100 tables with 1 row each.

**Watch for:**
- 100 segment files created on disk
- Startup time loading all of them

## What this reveals
- Error handling paths
- Memory behaviour with large strings
- Precision loss: INT64 → double promotion in AggState
- Startup cost scales with number of tables
