#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// felwood::MysqlServer – TCP accept loop + MySQL wire-protocol handler
//
// Listens on a given port, accepts one client at a time (one thread per
// connection), runs the MySQL handshake, then dispatches COM_QUERY commands.
//
// Only the demo GROUP BY / aggregate query is actually executed; all other
// statements (SET, SHOW, @@variables) receive minimal stub responses so that
// standard drivers complete their connection handshake without errors.
// ─────────────────────────────────────────────────────────────────────────────

#include "server/mysql_proto.hpp"
#include "storage/table.hpp"
#include "operators/scan.hpp"
#include "operators/filter.hpp"
#include "operators/aggregate.hpp"

#include <winsock2.h>

#include <atomic>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <algorithm>
#include <cctype>

namespace felwood {

class MysqlServer {
public:
    MysqlServer(uint16_t port, const Table& table)
        : port_(port), table_(table) {}

    void run() {
        WSADATA wsa{};
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
            throw std::runtime_error("WSAStartup failed");

        SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listener == INVALID_SOCKET)
            throw std::runtime_error("socket() failed: " + std::to_string(WSAGetLastError()));

        // Allow port reuse so restart is quick
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
    // ── Per-connection state machine ──────────────────────────────────────────
    void handle_connection(SOCKET sock, uint32_t conn_id) {
        try {
            // 1. Send server handshake
            uint8_t seq = 0;
            send_raw(sock, seq++, make_handshake_v10(conn_id));

            // 2. Read client HandshakeResponse41 — accept any credentials
            auto auth_pkt = recv_packet(sock);
            seq = auth_pkt.seq + 1;  // server=0, client=1, our OK must be seq=2
            send_ok(sock, seq);

            // 3. Command loop
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
                    // Unknown command — send OK
                    send_ok(sock, seq);
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "[conn " << conn_id << "] " << e.what() << '\n';
        }
        closesocket(sock);
    }

    // ── Query dispatcher ──────────────────────────────────────────────────────
    void dispatch_query(SOCKET sock, uint8_t& seq, const std::string& sql) {
        // Uppercase + trim for matching
        std::string upper = sql;
        std::transform(upper.begin(), upper.end(), upper.begin(),
                       [](unsigned char c) { return std::toupper(c); });
        // ltrim
        auto first = upper.find_first_not_of(" \t\r\n");
        if (first != std::string::npos) upper = upper.substr(first);

        // Stub responses for driver handshake statements
        if (starts_with(upper, "SET ")        ||
            starts_with(upper, "SET\t")       ||
            starts_with(upper, "USE ")        ||
            starts_with(upper, "USE\t")       ||
            starts_with(upper, "SELECT 1")    ||
            upper == "SELECT 1") {
            send_ok(sock, seq);
            return;
        }

        if (upper.find("@@VERSION") != std::string::npos ||
            upper.find("@@VERSION_COMMENT") != std::string::npos) {
            send_single_value(sock, seq, "@@version", "8.0.31-Felwood");
            return;
        }

        if (upper.find("@@") != std::string::npos) {
            // Generic @@variable stub — return empty string
            send_single_value(sock, seq, "var", "");
            return;
        }

        if (starts_with(upper, "SHOW DATABASES")) {
            send_single_value(sock, seq, "Database", "felwood");
            return;
        }

        if (starts_with(upper, "SHOW TABLES")) {
            send_single_value(sock, seq, "Tables_in_felwood", "employees");
            return;
        }

        if (upper.find("FROM EMPLOYEES") != std::string::npos) {
            auto result = run_demo_query();
            if (result) {
                send_result_set(sock, seq, *result);
            } else {
                send_ok(sock, seq);
            }
            return;
        }

        // Default: empty OK
        send_ok(sock, seq);
    }

    // ── Demo pipeline: Scan → Filter → Aggregate ─────────────────────────────
    std::optional<Chunk> run_demo_query() {
        auto scan = std::make_unique<ScanOperator>(
            table_,
            std::vector<std::string>{"department", "salary"}
        );

        auto filter = std::make_unique<FilterOperator>(
            std::move(scan),
            [](const Chunk& chunk, std::size_t row) -> bool {
                const auto& sal_vec =
                    std::get<std::vector<double>>(chunk.get_column("salary").data);
                return sal_vec[row] > 50'000.0;
            }
        );

        auto agg = std::make_unique<AggregateOperator>(
            std::move(filter),
            "department",
            std::vector<AggSpec>{
                {"salary", AggFunc::SUM,   "sum_salary"  },
                {"salary", AggFunc::COUNT, "count_salary"},
                {"salary", AggFunc::AVG,   "avg_salary"  },
            }
        );

        agg->open();
        std::optional<Chunk> result;
        if (auto maybe = agg->next()) result = std::move(maybe);
        agg->close();
        return result;
    }

    // ── Utility ───────────────────────────────────────────────────────────────
    static bool starts_with(const std::string& s, const std::string& prefix) {
        return s.size() >= prefix.size() &&
               s.compare(0, prefix.size(), prefix) == 0;
    }

    uint16_t      port_;
    const Table&  table_;
};

} // namespace felwood
