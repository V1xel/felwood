#pragma once

#include "storage/table.hpp"

#include <string>
#include <memory>
#include <unordered_map>
#include <stdexcept>

namespace felwood {
    class Catalog {
    public:
        void create_table(const std::string& name, TableSchema schema) {
            if (tables_.count(name))
                throw std::runtime_error("Catalog: table already exists: " + name);
            tables_.emplace(name, std::make_unique<Table>(name, std::move(schema)));
        }

        Table& get_table(const std::string& name) {
            auto it = tables_.find(name);
            if (it == tables_.end())
                throw std::runtime_error("Catalog: table not found: " + name);
            return *it->second;
        }

        bool has_table(const std::string& name) const {
            return tables_.count(name) > 0;
        }

        const std::unordered_map<std::string, std::unique_ptr<Table>>& all() const {
            return tables_;
        }

    private:
        std::unordered_map<std::string, std::unique_ptr<Table>> tables_;
    };
}
