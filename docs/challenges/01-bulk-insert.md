# Challenge 01 — Bulk Insert

## Goal
Measure how Felwood handles inserting large amounts of data and where it breaks down.

## Setup

```sql
CREATE TABLE events (id INT64, user_id INT64, amount FLOAT64, category STRING);
```

## Challenges

### 1.1 — 1 000 rows
Insert 1 000 rows. Verify they all come back with SELECT.

**Expected:** Works fine.

### 1.2 — 10 000 rows
Insert 10 000 rows one by one.

**Watch for:**
- How long does it take? (each INSERT rewrites the full segment file)
- Does the segment file size look right in ImHex?

### 1.3 — 100 000 rows
Insert 100 000 rows.

**Watch for:**
- Does it become noticeably slow? At what row count?
- Memory usage — all rows live in RAM

### 1.4 — Wide table
```sql
CREATE TABLE wide (a INT64, b INT64, c INT64, d FLOAT64, e FLOAT64, f STRING, g STRING, h BOOLEAN);
INSERT INTO wide VALUES (1, 2, 3, 1.1, 2.2, 'hello', 'world', 1);
```

**Watch for:**
- Does the ImHex pattern handle 8 columns correctly?

## What this reveals
- Rewrite-per-insert cost (no MemTable)
- Segment file growth rate
- Memory ceiling with no spill-to-disk
