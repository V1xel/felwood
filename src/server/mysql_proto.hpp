#pragma once

#include "common/types.hpp"
#include "common/column.hpp"

#include <winsock2.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <stdexcept>
#include <sstream>

namespace felwood {
    inline constexpr uint32_t CAP_LONG_PASSWORD     = 0x00000001;
    inline constexpr uint32_t CAP_PROTOCOL_41       = 0x00000200;
    inline constexpr uint32_t CAP_TRANSACTIONS      = 0x00002000;
    inline constexpr uint32_t CAP_SECURE_CONNECTION = 0x00008000;
    inline constexpr uint32_t CAP_PLUGIN_AUTH       = 0x00080000;

    inline constexpr uint32_t SERVER_CAPS =
        CAP_LONG_PASSWORD | CAP_PROTOCOL_41 |
        CAP_TRANSACTIONS  | CAP_SECURE_CONNECTION | CAP_PLUGIN_AUTH;

    inline constexpr uint8_t MYSQL_TYPE_TINY       = 0x01;
    inline constexpr uint8_t MYSQL_TYPE_DOUBLE     = 0x05;
    inline constexpr uint8_t MYSQL_TYPE_LONGLONG   = 0x08;
    inline constexpr uint8_t MYSQL_TYPE_VAR_STRING = 0xfd;

    inline constexpr uint8_t COM_QUIT  = 0x01;
    inline constexpr uint8_t COM_QUERY = 0x03;

    inline void write_le16(std::vector<uint8_t>& buf, uint16_t v) {
        buf.push_back(static_cast<uint8_t>(v & 0xff));
        buf.push_back(static_cast<uint8_t>((v >> 8) & 0xff));
    }

    inline void write_le32(std::vector<uint8_t>& buf, uint32_t v) {
        buf.push_back(static_cast<uint8_t>(v & 0xff));
        buf.push_back(static_cast<uint8_t>((v >>  8) & 0xff));
        buf.push_back(static_cast<uint8_t>((v >> 16) & 0xff));
        buf.push_back(static_cast<uint8_t>((v >> 24) & 0xff));
    }

    inline void write_lenenc_int(std::vector<uint8_t>& buf, uint64_t v) {
        if (v < 251) {
            buf.push_back(static_cast<uint8_t>(v));
        } else if (v < 65536) {
            buf.push_back(0xfc);
            write_le16(buf, static_cast<uint16_t>(v));
        } else if (v < 16777216) {
            buf.push_back(0xfd);
            buf.push_back(static_cast<uint8_t>(v & 0xff));
            buf.push_back(static_cast<uint8_t>((v >> 8) & 0xff));
            buf.push_back(static_cast<uint8_t>((v >> 16) & 0xff));
        } else {
            buf.push_back(0xfe);
            for (int i = 0; i < 8; ++i)
                buf.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xff));
        }
    }

    inline void write_lenenc_str(std::vector<uint8_t>& buf, const std::string& s) {
        write_lenenc_int(buf, s.size());
        buf.insert(buf.end(), s.begin(), s.end());
    }

    struct Packet {
        uint8_t              seq;
        std::vector<uint8_t> payload;
    };

    inline Packet recv_packet(SOCKET sock) {
        uint8_t hdr[4] = {};
        int total = 0;
        while (total < 4) {
            int n = recv(sock, reinterpret_cast<char*>(hdr) + total, 4 - total, 0);
            if (n <= 0) throw std::runtime_error("recv_packet: connection closed");
            total += n;
        }
        uint32_t length = static_cast<uint32_t>(hdr[0])
                        | (static_cast<uint32_t>(hdr[1]) << 8)
                        | (static_cast<uint32_t>(hdr[2]) << 16);
        uint8_t seq = hdr[3];

        std::vector<uint8_t> payload(length);
        total = 0;
        while (static_cast<uint32_t>(total) < length) {
            int n = recv(sock, reinterpret_cast<char*>(payload.data()) + total,
                         static_cast<int>(length) - total, 0);
            if (n <= 0) throw std::runtime_error("recv_packet: connection closed");
            total += n;
        }
        return {seq, std::move(payload)};
    }

    inline void send_raw(SOCKET sock, uint8_t seq, const std::vector<uint8_t>& payload) {
        uint32_t len = static_cast<uint32_t>(payload.size());
        uint8_t hdr[4] = {
            static_cast<uint8_t>(len & 0xff),
            static_cast<uint8_t>((len >> 8) & 0xff),
            static_cast<uint8_t>((len >> 16) & 0xff),
            seq
        };
        int sent = 0;
        while (sent < 4) {
            int n = send(sock, reinterpret_cast<char*>(hdr) + sent, 4 - sent, 0);
            if (n <= 0) throw std::runtime_error("send_raw: send failed");
            sent += n;
        }
        sent = 0;
        int total = static_cast<int>(payload.size());
        while (sent < total) {
            int n = send(sock, reinterpret_cast<const char*>(payload.data()) + sent,
                         total - sent, 0);
            if (n <= 0) throw std::runtime_error("send_raw: send failed");
            sent += n;
        }
    }

    inline std::vector<uint8_t> make_handshake_v10(uint32_t conn_id) {
        std::vector<uint8_t> p;
        p.push_back(10);

        const char* ver = "8.0.31-Felwood";
        for (const char* c = ver; *c; ++c) p.push_back(static_cast<uint8_t>(*c));
        p.push_back(0x00);

        write_le32(p, conn_id);

        const char scramble[] = "12345678";
        for (int i = 0; i < 8; ++i) p.push_back(static_cast<uint8_t>(scramble[i]));
        p.push_back(0x00);

        write_le16(p, static_cast<uint16_t>(SERVER_CAPS & 0xffff));

        p.push_back(0x21);
        write_le16(p, 0x0002);

        write_le16(p, static_cast<uint16_t>((SERVER_CAPS >> 16) & 0xffff));

        p.push_back(21);

        for (int i = 0; i < 10; ++i) p.push_back(0x00);

        const char scramble2[] = "1234567890123";
        for (int i = 0; i < 13; ++i) p.push_back(static_cast<uint8_t>(scramble2[i]));

        const char* plugin = "mysql_native_password";
        for (const char* c = plugin; *c; ++c) p.push_back(static_cast<uint8_t>(*c));
        p.push_back(0x00);

        return p;
    }

    inline std::vector<uint8_t> make_ok(uint8_t /*seq*/) {
        return {0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00};
    }

    inline std::vector<uint8_t> make_err(uint8_t /*seq*/, uint16_t code, const std::string& msg) {
        std::vector<uint8_t> p;
        p.push_back(0xff);
        write_le16(p, code);
        p.push_back('#');
        const char* state = "HY000";
        for (int i = 0; i < 5; ++i) p.push_back(static_cast<uint8_t>(state[i]));
        p.insert(p.end(), msg.begin(), msg.end());
        return p;
    }

    inline std::vector<uint8_t> make_eof(uint8_t /*seq*/) {
        return {0xfe, 0x00, 0x00, 0x02, 0x00};
    }

    inline uint8_t datatype_to_mysql(DataType dt) {
        switch (dt) {
            case DataType::INT64:   return MYSQL_TYPE_LONGLONG;
            case DataType::FLOAT64: return MYSQL_TYPE_DOUBLE;
            case DataType::STRING:  return MYSQL_TYPE_VAR_STRING;
            case DataType::BOOLEAN: return MYSQL_TYPE_TINY;
        }
        return MYSQL_TYPE_VAR_STRING;
    }

    inline std::vector<uint8_t> make_col_def(const std::string& col_name, DataType dt) {
        std::vector<uint8_t> p;
        write_lenenc_str(p, "def");
        write_lenenc_str(p, "");
        write_lenenc_str(p, "");
        write_lenenc_str(p, "");
        write_lenenc_str(p, col_name);
        write_lenenc_str(p, col_name);
        p.push_back(0x0c);
        write_le16(p, 0x21);
        write_le32(p, 255);
        p.push_back(datatype_to_mysql(dt));
        write_le16(p, 0x0000);
        p.push_back(0x00);
        write_le16(p, 0x0000);
        return p;
    }

    inline std::vector<uint8_t> make_text_row(const Chunk& chunk, std::size_t row_idx) {
        std::vector<uint8_t> p;
        for (const auto& col : chunk.columns) {
            std::string val_str;
            std::visit([&](const auto& vec) {
                using T = typename std::decay_t<decltype(vec)>::value_type;
                if constexpr (std::is_same_v<T, int64_t>) {
                    val_str = std::to_string(vec[row_idx]);
                } else if constexpr (std::is_same_v<T, double>) {
                    std::ostringstream oss;
                    oss << vec[row_idx];
                    val_str = oss.str();
                } else if constexpr (std::is_same_v<T, std::string>) {
                    val_str = vec[row_idx];
                } else if constexpr (std::is_same_v<T, bool>) {
                    val_str = vec[row_idx] ? "1" : "0";
                }
            }, col.data);
            write_lenenc_str(p, val_str);
        }
        return p;
    }

    inline void send_result_set(SOCKET sock, uint8_t& seq, const Chunk& chunk) {
        {
            std::vector<uint8_t> p;
            write_lenenc_int(p, chunk.columns.size());
            send_raw(sock, seq++, p);
        }
        for (const auto& col : chunk.columns)
            send_raw(sock, seq++, make_col_def(col.name, col.type));
        { uint8_t s = seq++; send_raw(sock, s, make_eof(s)); }
        for (std::size_t i = 0; i < chunk.num_rows; ++i)
            send_raw(sock, seq++, make_text_row(chunk, i));
        { uint8_t s = seq++; send_raw(sock, s, make_eof(s)); }
    }

    inline void send_single_value(SOCKET sock, uint8_t& seq,
                                   const std::string& col_name,
                                   const std::string& value) {
        Chunk c;
        c.num_rows = 1;
        Column col(col_name, DataType::STRING);
        std::get<std::vector<std::string>>(col.data).push_back(value);
        c.columns.push_back(std::move(col));
        send_result_set(sock, seq, c);
    }

    inline void send_ok(SOCKET sock, uint8_t& seq) {
        uint8_t s = seq++;
        send_raw(sock, s, make_ok(s));
    }
}
