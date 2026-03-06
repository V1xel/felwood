# Challenge 05 — Type Coverage

## Goal
Exercise all four data types across insert, filter, and aggregation.

## Challenges

### 5.1 — INT64
```sql
CREATE TABLE counters (name STRING, value INT64);
INSERT INTO counters VALUES ('hits', 1000000);
INSERT INTO counters VALUES ('misses', 42);
SELECT name, SUM(value) AS total FROM counters GROUP BY name;
```

### 5.2 — FLOAT64
```sql
CREATE TABLE prices (item STRING, price FLOAT64);
INSERT INTO prices VALUES ('apple', 0.99);
INSERT INTO prices VALUES ('laptop', 1299.99);
SELECT item, AVG(price) AS avg FROM prices GROUP BY item;
```

### 5.3 — STRING
```sql
CREATE TABLE tags (id INT64, tag STRING);
INSERT INTO tags VALUES (1, 'database');
INSERT INTO tags VALUES (2, 'olap');
SELECT tag FROM tags WHERE tag = 'olap';
```

### 5.4 — BOOLEAN
```sql
CREATE TABLE flags (id INT64, active BOOLEAN);
INSERT INTO flags VALUES (1, 1);
INSERT INTO flags VALUES (2, 0);
SELECT id FROM flags WHERE active = 1;
```

### 5.5 — Mixed types in one table
```sql
CREATE TABLE mixed (id INT64, score FLOAT64, label STRING, valid BOOLEAN);
INSERT INTO mixed VALUES (1, 9.5, 'A', 1);
INSERT INTO mixed VALUES (2, 4.2, 'B', 0);
SELECT label, SUM(score) AS total FROM mixed WHERE valid = 1 GROUP BY label;
```

## What this reveals
- BOOLEAN stored as 1 byte per row (WHERE active = 1 not WHERE active = true)
- FLOAT64 precision — standard IEEE 754 double
- STRING compared byte-by-byte, case-sensitive
