# Type System

Defined in `src/common/types.hpp`.

## DataType

Runtime type tag for scalar values. Uses `uint8_t` as the underlying type to keep metadata compact.

| Enumerator | Value | C++ type |
|------------|-------|----------|
| `INT64`    | 0     | `int64_t` |
| `FLOAT64`  | 1     | `double` |
| `STRING`   | 2     | `std::string` |
| `BOOLEAN`  | 3     | `bool` |

## Value

`Value` is a `std::variant` over the four physical types, in the same order as the `DataType` enumerators. This means `v.index() == static_cast<size_t>(DataType)` always holds, and `value_type(v)` exploits this to recover the tag in O(1) without a separate field.

```cpp
using Value = std::variant<int64_t, double, std::string, bool>;
```

## Helper Functions

| Function | Description |
|----------|-------------|
| `value_type(v)` | Returns the `DataType` tag of a `Value` |
| `type_name(dt)` | Returns a printable `string_view` for a `DataType` |
| `value_to_string(v)` | Converts any `Value` to a `std::string` for display |

## Limitations and Future Work

- **NULL** is not represented. A real engine uses a separate validity bitmap (one bit per row) so that nullability does not require sentinel values or `std::optional` wrappers on every cell.
- **Additional types**: DECIMAL, DATE32, TIMESTAMP64, LIST, STRUCT are absent.
- **Physical vs logical type distinction**: e.g., a dictionary-encoded STRING column has logical type STRING but physical type INT32 (dictionary index). This separation enables DICT encoding without touching the logical layer.
- **Arrow compatibility**: aligning type IDs with Apache Arrow would allow zero-copy import of Arrow record batches.
