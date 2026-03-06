#pragma once

#include "server/mysql_proto.hpp"
#include "storage/catalog.hpp"
#include "sql/lexer.hpp"
#include "sql/parser.hpp"
#include "sql/planner.hpp"

#include <winsock2.h>

#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <algorithm>
#include <cctype>

namespace felwood {
    class MysqlServer {
    public:
        MysqlServer(uint16_t port, Catalog& catalog)
            : port_(port), catalog_(catalog) {}

        void run() {
            WSADATA wsa{};
            if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
                throw std::runtime_error("WSAStartup failed");

            SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (listener == INVALID_SOCKET)
                throw std::runtime_error("socket() failed: " + std::to_string(WSAGetLastError()));

            int opt = 1;
            setsockopt(listener, SOL_SOCKET, SO_REUSEADDR,
                       reinterpret_cast<const char*>(&opt), sizeof(opt));

            sockaddr_in addr{};
            addr.sin_family      = AF_INET;
            addr.sin_addr.s_addr = INADDR_ANY;
            addr.sin_port        = htons(port_);

            if (bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR)
                throw std::runtime_error("bind() failed: " + std::to_string(WSAGetLastError()));

            if (listen(listener, SOMAXCONN) == SOCKET_ERROR)
                throw std::runtime_error("listen() failed: " + std::to_string(WSAGetLastError()));

            uint32_t conn_id = 1;
            while (true) {
                SOCKET client = accept(listener, nullptr, nullptr);
                if (client == INVALID_SOCKET) continue;

                uint32_t cid = conn_id++;
                std::thread([this, client, cid]() {
                    handle_connection(client, cid);
                }).detach();
            }

            closesocket(listener);
            WSACleanup();
        }

    private:
        void handle_connection(SOCKET sock, uint32_t conn_id) {
            try {
                uint8_t seq = 0;
                send_raw(sock, seq++, make_handshake_v10(conn_id));

                auto auth_pkt = recv_packet(sock);
                seq = auth_pkt.seq + 1;
                send_ok(sock, seq);

                while (true) {
                    Packet pkt = recv_packet(sock);
                    if (pkt.payload.empty()) break;

                    uint8_t cmd = pkt.payload[0];
                    seq = pkt.seq + 1;

                    if (cmd == COM_QUIT) break;

                    if (cmd == COM_QUERY) {
                        std::string sql(reinterpret_cast<char*>(pkt.payload.data() + 1),
                                        pkt.payload.size() - 1);
                        dispatch_query(sock, seq, sql);
                    } else {
                        send_ok(sock, seq);
                    }
                }
            } catch (const std::exception& e) {
                std::cerr << "[conn " << conn_id << "] " << e.what() << '\n';
            }
            closesocket(sock);
        }

        void dispatch_query(SOCKET sock, uint8_t& seq, const std::string& sql) {
            std::string upper = sql;
            std::transform(upper.begin(), upper.end(), upper.begin(),
                           [](unsigned char c) { return std::toupper(c); });
            auto first = upper.find_first_not_of(" \t\r\n");
            if (first != std::string::npos) upper = upper.substr(first);

            if (starts_with(upper, "SET ")     || starts_with(upper, "SET\t")  ||
                starts_with(upper, "USE ")     || starts_with(upper, "USE\t")  ||
                starts_with(upper, "SELECT 1") || upper == "SELECT 1") {
                send_ok(sock, seq);
                return;
            }
            if (upper.find("@@VERSION") != std::string::npos ||
                upper.find("@@VERSION_COMMENT") != std::string::npos) {
                send_single_value(sock, seq, "@@version", "8.0.31-Felwood");
                return;
            }
            if (upper.find("@@") != std::string::npos) {
                send_single_value(sock, seq, "var", "");
                return;
            }
            if (starts_with(upper, "SHOW DATABASES")) {
                send_single_value(sock, seq, "Database", "felwood");
                return;
            }
            if (starts_with(upper, "SHOW TABLES")) {
                std::string names;
                for (const auto& [name, _] : catalog_.all())
                    names += name + ",";
                if (!names.empty()) names.pop_back();
                send_single_value(sock, seq, "Tables_in_felwood", names);
                return;
            }

            try {
                Lexer   lexer(sql);
                Parser  parser(lexer.tokenize());
                Planner planner(catalog_);

                auto op = planner.plan(parser.parse());

                if (op) {
                    auto result = drain(**op);
                    if (result)
                        send_result_set(sock, seq, *result);
                    else
                        send_ok(sock, seq);
                } else {
                    send_ok(sock, seq);
                }
            } catch (const std::exception& e) {
                send_raw(sock, seq++, make_err(0, 1064, e.what()));
            }
        }

        static std::optional<Chunk> drain(Operator& op) {
            op.open();
            std::vector<Chunk> chunks;
            while (auto c = op.next()) chunks.push_back(std::move(*c));
            op.close();
            return merge(std::move(chunks));
        }

        static std::optional<Chunk> merge(std::vector<Chunk> chunks) {
            if (chunks.empty()) return std::nullopt;
            if (chunks.size() == 1) return std::move(chunks[0]);

            Chunk out;
            for (const auto& col : chunks[0].columns)
                out.columns.emplace_back(col.name, col.type);

            for (auto& chunk : chunks) {
                for (std::size_t r = 0; r < chunk.num_rows; ++r)
                    for (std::size_t c = 0; c < chunk.columns.size(); ++c)
                        out.columns[c].append(chunk.columns[c].get(r));
                out.num_rows += chunk.num_rows;
            }
            return out;
        }

        static bool starts_with(const std::string& s, const std::string& prefix) {
            return s.size() >= prefix.size() &&
                   s.compare(0, prefix.size(), prefix) == 0;
        }

        uint16_t  port_;
        Catalog&  catalog_;
    };
}
