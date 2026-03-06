#include "storage/catalog.hpp"
#include "sql/lexer.hpp"
#include "sql/parser.hpp"
#include "sql/planner.hpp"
#include "server/mysql_server.hpp"

#include <iostream>
#include <string>

using namespace felwood;

static void run_sql(const std::string& sql, Catalog& catalog) {
    Lexer   lexer(sql);
    Parser  parser(lexer.tokenize());
    Planner planner(catalog);
    planner.plan(parser.parse());
}

int main() {
    Catalog catalog("felwood_data");

    MysqlServer server(3306, catalog);
    std::cout << "Felwood listening on port 3306 ...\n";
    server.run();
    return 0;
}
