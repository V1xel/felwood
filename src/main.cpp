#include "storage/catalog.hpp"
#include "sql/lexer.hpp"
#include "sql/parser.hpp"
#include "sql/planner.hpp"
#include "server/mysql_server.hpp"

#include <iostream>
#include <string>

using namespace felwood;

int main() {
    Catalog catalog("felwood_data");

    MysqlServer server(3306, catalog);
    std::cout << "Felwood listening on port 3306 ...\n";
    server.run();
    return 0;
}
