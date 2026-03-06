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

`Value` is a `std::variant` over the four physical types, in the same order as the `DataType` enumerators. `v.index() == static_cast<size_t>(DataType)` always holds.

```cpp
using Value = std::variant<int64_t, double, std::string, bool>;
```

## Helper Functions

| Function | Description |
|----------|-------------|
| `value_type(v)` | Returns the `DataType` tag of a `Value` |
| `type_name(dt)` | Returns a printable `string_view` for a `DataType` |
| `value_to_string(v)` | Converts any `Value` to a `std::string` for display |
