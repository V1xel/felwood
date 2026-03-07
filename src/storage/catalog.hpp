#pragma once

#include "storage/table.hpp"
#include "storage/segment_manager.hpp"

#include <string>
#include <memory>
#include <optional>
#include <unordered_map>
#include <stdexcept>

namespace felwood {
    class Catalog {
    public:
        Catalog() = default;

        explicit Catalog(std::string data_dir)
            : storage_(std::make_unique<SegmentManager>(std::move(data_dir)))
        {
            for (auto& tbl : storage_->load_all())
                tables_.emplace(tbl->name, std::move(tbl));
        }

        void create_table(const std::string& name, TableSchema schema) {
            if (tables_.count(name))
                throw std::runtime_error("Catalog: table already exists: " + name);
            uint64_t id = storage_ ? storage_->assign_id(name) : 0;
            auto tbl = std::make_unique<Table>(name, std::move(schema));
            tbl->id = id;
            auto& ref = *tables_.emplace(name, std::move(tbl)).first->second;
            if (storage_) storage_->flush(ref);
        }

        void insert_row(const std::string& name, const std::vector<Value>& row) {
            Table& tbl = get_table(name);
            tbl.append_row(row);
            if (storage_) storage_->flush(tbl);
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
        std::unique_ptr<SegmentManager> storage_;
    };
}
