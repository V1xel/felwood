// ─────────────────────────────────────────────────────────────────────────────
// Felwood – single-node columnar OLAP execution engine  (demo entry point)
//
// Demonstrates the full query pipeline for:
//
//   SELECT department,
//          SUM(salary)   AS sum_salary,
//          COUNT(salary) AS count_salary,
//          AVG(salary)   AS avg_salary
//   FROM   employees
//   WHERE  salary > 50000
//   GROUP  BY department
//
// Operator tree (Volcano / pull model, bottom → top):
//
//   AggregateOperator  ← GROUP BY department, SUM/COUNT/AVG salary
//         ↑
//   FilterOperator     ← salary > 50 000
//         ↑
//   ScanOperator       ← projects {department, salary} from Table
//         ↑
//       Table          ← in-memory columnar store (employees)
//
// In a real engine the query text would pass through a parser (Bison/ANTLR),
// a binder (resolves names to catalog entries), a logical planner, a rule-based
// optimizer (predicate pushdown, column pruning), a cost-based optimizer
// (cardinality estimation, join reordering), and finally a physical plan
// builder that emits operator instances like those below.
// ─────────────────────────────────────────────────────────────────────────────

#include "common/types.hpp"
#include "common/column.hpp"
#include "storage/table.hpp"
#include "operators/operator.hpp"
#include "operators/scan.hpp"
#include "operators/filter.hpp"
#include "operators/aggregate.hpp"
#include "server/mysql_server.hpp"

#include <iostream>
#include <memory>
#include <vector>
#include <string>

using namespace felwood;

// ── Build the employees table ─────────────────────────────────────────────────
// In a real engine this data would be read from Parquet/ORC files on disk.
// The table has three columns: id (INT64), department (STRING), salary (FLOAT64).
static Table build_employees() {
    Schema s;
    s.add("id",         DataType::INT64);
    s.add("department", DataType::STRING);
    s.add("salary",     DataType::FLOAT64);

    Table t("employees", s);

    // Salary values that exceed 50 000 are marked ✓; others will be filtered.
    //                      id   department        salary
    t.append_row({ Value{int64_t(1)},  Value{std::string("Engineering")}, Value{95000.0}  }); // ✓
    t.append_row({ Value{int64_t(2)},  Value{std::string("Engineering")}, Value{85000.0}  }); // ✓
    t.append_row({ Value{int64_t(3)},  Value{std::string("Marketing")},   Value{60000.0}  }); // ✓
    t.append_row({ Value{int64_t(4)},  Value{std::string("Marketing")},   Value{45000.0}  }); // ✗ filtered
    t.append_row({ Value{int64_t(5)},  Value{std::string("HR")},          Value{55000.0}  }); // ✓
    t.append_row({ Value{int64_t(6)},  Value{std::string("HR")},          Value{48000.0}  }); // ✗ filtered
    t.append_row({ Value{int64_t(7)},  Value{std::string("Engineering")}, Value{105000.0} }); // ✓
    t.append_row({ Value{int64_t(8)},  Value{std::string("Sales")},       Value{70000.0}  }); // ✓
    t.append_row({ Value{int64_t(9)},  Value{std::string("Sales")},       Value{52000.0}  }); // ✓
    t.append_row({ Value{int64_t(10)}, Value{std::string("Marketing")},   Value{65000.0}  }); // ✓
    t.append_row({ Value{int64_t(11)}, Value{std::string("Engineering")}, Value{90000.0}  }); // ✓
    t.append_row({ Value{int64_t(12)}, Value{std::string("Engineering")}, Value{78000.0}  }); // ✓
    t.append_row({ Value{int64_t(13)}, Value{std::string("Marketing")},   Value{72000.0}  }); // ✓
    t.append_row({ Value{int64_t(14)}, Value{std::string("Sales")},       Value{80000.0}  }); // ✓
    t.append_row({ Value{int64_t(15)}, Value{std::string("HR")},          Value{61000.0}  }); // ✓

    return t;
}

// ── Entry point ───────────────────────────────────────────────────────────────
int main() {
    Table employees = build_employees();
    felwood::MysqlServer server(3306, employees);
    std::cout << "Felwood listening on port 3306 ...\n";
    server.run();   // blocks
    return 0;
}
