# SQL Layer

Defined in `src/sql/` and `src/storage/catalog.hpp`.

## Supported SQL

```sql
CREATE TABLE name (col TYPE, ...)
INSERT INTO name VALUES (val, ...)
SELECT col [AS alias], AGG(col) [AS alias], ... FROM table
    [WHERE col OP val [AND col OP val ...]]
    [GROUP BY col]
```

Types: `INT64`, `FLOAT64`, `STRING`, `BOOLEAN`

Aggregate functions: `SUM`, `COUNT`, `MIN`, `MAX`, `AVG`

Comparison operators: `=`, `!=`, `<`, `>`, `<=`, `>=`

---

## Lexer (`lexer.hpp`)

Converts a SQL string into a flat list of `Token` objects. One pass, left to right.

Each `Token` has:
- `type` — what kind of token it is (`TokenType` enum)
- `text` — original text from the source
- `literal` — parsed `Value`, meaningful only for `INT_LIT`, `FLOAT_LIT`, `STRING_LIT`

`TokenType` groups:

| Group | Values |
|-------|--------|
| Keywords | `CREATE TABLE INSERT INTO VALUES SELECT FROM WHERE GROUP BY AS AND` |
| Aggregates | `SUM COUNT MIN MAX AVG` |
| Type keywords | `KW_INT64 KW_FLOAT64 KW_STRING KW_BOOLEAN` |
| Operators | `EQ NEQ LT GT LEQ GEQ` |
| Punctuation | `LPAREN RPAREN COMMA SEMICOLON` |
| Literals | `INT_LIT FLOAT_LIT STRING_LIT` |
| Other | `IDENT EOF_TOK` |

Keywords are case-insensitive — the lexer uppercases the word before looking it up in the keyword map.

---

## AST (`ast.hpp`)

Plain structs representing the parsed statement. No methods, no inheritance.

**Expressions (used in WHERE):**

| Type | Fields | Description |
|------|--------|-------------|
| `ColumnRef` | `name` | Reference to a column by name |
| `Literal` | `val` | A constant value |
| `BinaryExpr` | `op`, `left`, `right` | A comparison between two operands |

**SELECT list item:**

| Field | Type | Description |
|-------|------|-------------|
| `agg` | `optional<AggFunc>` | Aggregate function; nullopt = plain column |
| `col` | `string` | Source column name |
| `alias` | `string` | Output name; same as `col` if no `AS` |

**Statements:**

| Type | Fields |
|------|--------|
| `CreateTableStmt` | `name`, `cols` (vector of `ColumnSchema`) |
| `InsertStmt` | `table`, `values` (vector of `Value`) |
| `SelectStmt` | `items`, `from`, `where`, `group_by` |

`Stmt` is a `std::variant<CreateTableStmt, InsertStmt, SelectStmt>`.

---

## Parser (`parser.hpp`)

Recursive descent — each grammar rule maps to one private method.

Entry point: `parse()` — looks at the first token to decide which statement to parse.

| Method | Parses |
|--------|--------|
| `parse_create()` | `CREATE TABLE name (col type, ...)` |
| `parse_insert()` | `INSERT INTO name VALUES (val, ...)` |
| `parse_select()` | `SELECT ... FROM ... [WHERE ...] [GROUP BY ...]` |
| `parse_select_item()` | One item in the SELECT list |
| `parse_condition()` | One `col OP val` expression |
| `parse_operand()` | Column reference or literal |
| `parse_type()` | Type keyword → `DataType` |
| `parse_literal()` | Literal token → `Value` |

Helper methods:
- `peek()` — look at current token without consuming
- `consume()` — take current token and advance
- `expect(t)` — consume and throw if type doesn't match
- `match(t)` — consume only if type matches, return bool

---

## Catalog (`catalog.hpp`)

Stores all live tables by name. Owns the `Table` objects via `unique_ptr`. Optionally backed by a `SegmentManager` for persistence.

| Method | Description |
|--------|-------------|
| `Catalog()` | In-memory only |
| `Catalog(data_dir)` | Persistent: loads all tables from disk on construction |
| `create_table(name, schema)` | Create a new empty table; throws if name already exists; flushes to disk if persistent |
| `insert_row(name, values)` | Append a row; flushes to disk if persistent |
| `get_table(name)` | Returns a reference; throws if not found |
| `has_table(name)` | Check existence without throwing |
| `all()` | Read-only access to the full table map |

---

## Planner (`planner.hpp`)

Turns a `Stmt` into an operator tree (for SELECT) or executes it immediately (for CREATE/INSERT).

Entry point: `plan(stmt)` — dispatches via `std::visit` to the appropriate `execute()` overload.

**CREATE TABLE:** builds a `TableSchema` from the AST column list, calls `catalog_.create_table`.

**INSERT:** calls `catalog_.insert_row` with the values from the AST.

**SELECT:** builds the operator tree bottom-up:

```
ScanOperator(projected columns)
  → [FilterOperator(WHERE conditions)]   if WHERE present
    → [AggregateOperator(GROUP BY + aggs)] if GROUP BY present
```

`collect_columns` gathers all column names referenced in SELECT, WHERE, and GROUP BY to pass as the projection list to `ScanOperator`.

**Expression evaluation (used inside the filter predicate):**

| Function | Description |
|----------|-------------|
| `eval_operand` | Reads a column value from the chunk or returns a literal |
| `eval_condition` | Evaluates one `BinaryExpr` for a given row |
| `compare_values` | Compares two `Value`s; numerics coerced to `double`, strings lexicographic |
