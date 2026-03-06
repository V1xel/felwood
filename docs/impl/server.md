# Server

Defined in `src/server/`.

## MySQL Wire Protocol (`mysql_proto.hpp`)

Pure serialisation / deserialisation helpers — no sockets. Implements enough of the MySQL client-server protocol (protocol version 10, "MySQL 4.1+" packet format) for a single-query demo server.

### Capability flags

| Constant | Value | Meaning |
|----------|-------|---------|
| `CAP_LONG_PASSWORD` | `0x00000001` | Support for long passwords |
| `CAP_PROTOCOL_41` | `0x00000200` | MySQL 4.1+ protocol |
| `CAP_TRANSACTIONS` | `0x00002000` | Transaction support |
| `CAP_SECURE_CONNECTION` | `0x00008000` | Secure auth |
| `CAP_PLUGIN_AUTH` | `0x00080000` | Auth plugin |

`SERVER_CAPS` ORs all of the above.

### MySQL column type codes

| Constant | Value | Maps from |
|----------|-------|-----------|
| `MYSQL_TYPE_TINY` | `0x01` | BOOLEAN |
| `MYSQL_TYPE_DOUBLE` | `0x05` | FLOAT64 |
| `MYSQL_TYPE_LONGLONG` | `0x08` | INT64 |
| `MYSQL_TYPE_VAR_STRING` | `0xfd` | STRING |

### Command bytes

| Constant | Value | Meaning |
|----------|-------|---------|
| `COM_QUIT` | `0x01` | Client disconnect |
| `COM_QUERY` | `0x03` | SQL query |

### Low-level byte writers

| Function | Description |
|----------|-------------|
| `write_le16(buf, v)` | Append a 16-bit little-endian integer |
| `write_le32(buf, v)` | Append a 32-bit little-endian integer |
| `write_lenenc_int(buf, v)` | Append a length-encoded integer (MySQL format) |
| `write_lenenc_str(buf, s)` | Append a length-encoded string |

### Packet framing

| Function / Type | Description |
|-----------------|-------------|
| `Packet { seq, payload }` | One MySQL packet: sequence ID + raw payload bytes |
| `recv_packet(sock)` | Read one complete packet from socket (blocking) |
| `send_raw(sock, seq, payload)` | Frame and send a packet (3-byte length + seq byte) |

### Packet builders

| Function | Description |
|----------|-------------|
| `make_handshake_v10(conn_id)` | Server Handshake v10 payload |
| `make_ok(seq)` | OK packet (affected_rows=0, last_insert_id=0) |
| `make_err(seq, code, msg)` | ERR packet with SQL state `HY000` |
| `make_eof(seq)` | EOF packet (PROTOCOL_41 format) |
| `make_col_def(col_name, dt)` | ColumnDefinition41 packet |
| `make_text_row(chunk, row_idx)` | TextResultRow: all values as length-encoded strings |
| `datatype_to_mysql(dt)` | Map `DataType` to MySQL type byte |

### Result set senders

| Function | Description |
|----------|-------------|
| `send_result_set(sock, seq, chunk)` | Send a complete result set for a `Chunk` |
| `send_single_value(sock, seq, col_name, value)` | One-column, one-row result set |
| `send_ok(sock, seq)` | Send an OK packet |

---

## MysqlServer (`mysql_server.hpp`)

TCP accept loop with MySQL wire-protocol handling. Listens on a configured port, accepts connections (one thread per connection), runs the MySQL handshake, then dispatches `COM_QUERY` commands.

### Connection state machine (`handle_connection`)

1. Send Server Handshake v10.
2. Read client `HandshakeResponse41` — accept any credentials.
3. Send OK.
4. Loop: read command packets.
   - `COM_QUIT` → close.
   - `COM_QUERY` → `dispatch_query`.
   - Anything else → OK.

### Query dispatcher (`dispatch_query`)

Uppercases and left-trims the SQL string, then matches against a priority list:

| Match | Response |
|-------|----------|
| `SET …` / `USE …` / `SELECT 1` | OK |
| `@@VERSION` / `@@VERSION_COMMENT` | Single-value result: `"8.0.31-Felwood"` |
| Any `@@variable` | Single-value result: `""` |
| `SHOW DATABASES` | Single-value result: `"felwood"` |
| `SHOW TABLES` | Single-value result: `"employees"` |
| Contains `FROM EMPLOYEES` | Run demo pipeline, send result set |
| Default | OK |
