# Challenge 04 — Persistence

## Goal
Verify that data survives process restart and test crash-safety limits.

## Challenges

### 4.1 — Basic round-trip
```sql
CREATE TABLE config (key STRING, value STRING);
INSERT INTO config VALUES ('version', '1.0');
INSERT INTO config VALUES ('env', 'production');
```
Restart Felwood. Then:
```sql
SELECT key, value FROM config;
```
**Expected:** Both rows returned.

### 4.2 — Large table survives restart
Insert 10 000 rows, restart, SELECT COUNT.

**Expected:** All rows present. If count is wrong, the segment file was not fully flushed.

### 4.3 — Crash safety (no WAL)
Start an INSERT of many rows. Kill the process mid-way (Task Manager or Ctrl+C during bulk insert).

Restart and SELECT.

**Watch for:**
- Rows written before the kill survive (segment was flushed after each INSERT)
- The last in-flight INSERT may be missing if killed mid-flush
- No partial/corrupt rows — the segment file is rewritten atomically per INSERT

### 4.4 — Segment file inspection
After inserting rows, open `felwood_data/<table>/segment.seg` in ImHex with the pattern file.

**Verify:**
- Opening and closing `FLWD` magic bytes present
- Footer shows correct `num_rows` and `num_columns`
- Column offsets in footer match the actual byte positions
- Column names and types match the schema

## What this reveals
- Each INSERT is durable (full rewrite before returning)
- No WAL means a crash mid-flush loses at most one row
- No partial writes visible to readers (file is truncated and rewritten atomically)
