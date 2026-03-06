#pragma endian little

import std.mem;
import std.core;

// ── Types ─────────────────────────────────────────────────────────────────────

enum DataType : u8 {
    INT64   = 0,
    FLOAT64 = 1,
    STRING  = 2,
    BOOLEAN = 3,
};

struct FooterColumn {
    u8       name_len;
    char     name[name_len];
    DataType type;
    u64      data_offset;
    u64      data_size;
};

struct Footer {
    u64          num_rows;
    u32          num_columns;
    FooterColumn columns[num_columns];
};

struct StringEntry {
    u32  len;
    char value[len];
};

// ── Parse footer first ────────────────────────────────────────────────────────

u32    footer_size   @ (std::mem::size() - 8);
u32    closing_magic @ (std::mem::size() - 4);
Footer footer        @ (std::mem::size() - 8 - footer_size);
u32    opening_magic @ 0x00;

// ── Column data array ─────────────────────────────────────────────────────────
// Each element uses std::core::array_index() to look up its own
// offset and size from the already-parsed footer.

struct ColData {
    u64 i = std::core::array_index();

    if (footer.columns[i].type == DataType::STRING)
        StringEntry entries[footer.num_rows];
    else if (footer.columns[i].type == DataType::BOOLEAN)
        u8 values[footer.columns[i].data_size];
    else
        u64 values[footer.columns[i].data_size / 8];
} [[inline]];

ColData columns[footer.num_columns] @ footer.columns[0].data_offset;
