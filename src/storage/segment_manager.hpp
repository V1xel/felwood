#pragma once

#include "storage/table.hpp"

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include <unordered_map>

namespace felwood {

namespace fs = std::filesystem;

class SegmentManager {
public:
    static constexpr uint32_t MAGIC = 0x464C5744;

    explicit SegmentManager(std::string data_dir)
        : root_(std::move(data_dir))
    {
        fs::create_directories(root_);
        load_catalog();
    }

    // Assigns a new numeric ID to the given table name and persists the mapping.
    uint64_t assign_id(const std::string& name) {
        uint64_t id = next_id_++;
        name_to_id_[name] = id;
        save_catalog();
        return id;
    }

    void flush(const Table& table) const {
        fs::path dir = table_dir(table.id);
        fs::create_directories(dir);

        std::ofstream f(dir / "segment.seg", std::ios::binary | std::ios::trunc);
        if (!f) throw std::runtime_error("StorageManager: cannot write segment for " + table.name);

        f.write(reinterpret_cast<const char*>(&MAGIC), 4);

        struct ColMeta { uint64_t offset; uint64_t size; };
        std::vector<ColMeta> metas;
        metas.reserve(table.columns.size());

        for (const auto& col : table.columns) {
            uint64_t start = static_cast<uint64_t>(f.tellp());
            write_column(f, col);
            uint64_t end   = static_cast<uint64_t>(f.tellp());
            metas.push_back({start, end - start});
        }

        uint64_t footer_start = static_cast<uint64_t>(f.tellp());

        uint64_t num_rows = static_cast<uint64_t>(table.num_rows());
        uint32_t num_cols = static_cast<uint32_t>(table.num_cols());
        f.write(reinterpret_cast<const char*>(&num_rows), 8);
        f.write(reinterpret_cast<const char*>(&num_cols), 4);

        for (std::size_t i = 0; i < table.schema.columns.size(); ++i) {
            const auto& col  = table.schema.columns[i];
            uint8_t name_len = static_cast<uint8_t>(col.name.size());
            uint8_t type     = static_cast<uint8_t>(col.type);
            f.write(reinterpret_cast<const char*>(&name_len),        1);
            f.write(col.name.data(),                                  name_len);
            f.write(reinterpret_cast<const char*>(&type),            1);
            f.write(reinterpret_cast<const char*>(&metas[i].offset), 8);
            f.write(reinterpret_cast<const char*>(&metas[i].size),   8);
        }

        uint64_t footer_end  = static_cast<uint64_t>(f.tellp());
        uint32_t footer_size = static_cast<uint32_t>(footer_end - footer_start);

        f.write(reinterpret_cast<const char*>(&footer_size), 4);
        f.write(reinterpret_cast<const char*>(&MAGIC),       4);
    }

    [[nodiscard]] std::vector<std::unique_ptr<Table>> load_all() const {
        std::vector<std::unique_ptr<Table>> tables;
        for (const auto& [name, id] : name_to_id_) {
            auto tbl = load_segment(id, name);
            if (tbl) tables.push_back(std::move(tbl));
        }
        return tables;
    }

private:
    std::string root_;
    std::unordered_map<std::string, uint64_t> name_to_id_;
    uint64_t next_id_ = 1;

    [[nodiscard]] fs::path catalog_path() const {
        return fs::path(root_) / "catalog.dat";
    }

    [[nodiscard]] fs::path table_dir(uint64_t id) const {
        return fs::path(root_) / std::to_string(id);
    }

    void save_catalog() const {
        std::ofstream f(catalog_path(), std::ios::binary | std::ios::trunc);
        if (!f) throw std::runtime_error("SegmentManager: cannot write catalog.dat");

        f.write(reinterpret_cast<const char*>(&next_id_), 8);
        uint32_t count = static_cast<uint32_t>(name_to_id_.size());
        f.write(reinterpret_cast<const char*>(&count), 4);

        for (const auto& [name, id] : name_to_id_) {
            f.write(reinterpret_cast<const char*>(&id), 8);
            uint8_t len = static_cast<uint8_t>(name.size());
            f.write(reinterpret_cast<const char*>(&len), 1);
            f.write(name.data(), len);
        }
    }

    void load_catalog() {
        fs::path path = catalog_path();
        if (!fs::exists(path)) return;

        std::ifstream f(path, std::ios::binary);
        if (!f) return;

        f.read(reinterpret_cast<char*>(&next_id_), 8);
        uint32_t count = 0;
        f.read(reinterpret_cast<char*>(&count), 4);

        for (uint32_t i = 0; i < count; ++i) {
            uint64_t id = 0;
            f.read(reinterpret_cast<char*>(&id), 8);
            uint8_t len = 0;
            f.read(reinterpret_cast<char*>(&len), 1);
            std::string name(len, '\0');
            f.read(name.data(), len);
            name_to_id_[name] = id;
        }
    }

    static void write_column(std::ofstream& f, const Column& col) {
        std::visit([&](const auto& vec) {
            using T = typename std::decay_t<decltype(vec)>::value_type;
            for (std::size_t i = 0; i < vec.size(); ++i) {
                if constexpr (std::is_same_v<T, std::string>) {
                    uint32_t len = static_cast<uint32_t>(vec[i].size());
                    f.write(reinterpret_cast<const char*>(&len), 4);
                    f.write(vec[i].data(), len);
                } else if constexpr (std::is_same_v<T, bool>) {
                    uint8_t b = vec[i] ? 1 : 0;
                    f.write(reinterpret_cast<const char*>(&b), 1);
                } else {
                    T val = vec[i];
                    f.write(reinterpret_cast<const char*>(&val), sizeof(T));
                }
            }
        }, col.data);
    }

    [[nodiscard]] std::unique_ptr<Table> load_segment(uint64_t id, const std::string& name) const {
        fs::path path = table_dir(id) / "segment.seg";
        if (!fs::exists(path)) return nullptr;

        std::ifstream f(path, std::ios::binary);
        if (!f) return nullptr;

        f.seekg(-4, std::ios::end);
        uint32_t magic = 0;
        f.read(reinterpret_cast<char*>(&magic), 4);
        if (magic != MAGIC)
            throw std::runtime_error("SegmentManager: bad magic in " + path.string());

        f.seekg(-8, std::ios::end);
        uint32_t footer_size = 0;
        f.read(reinterpret_cast<char*>(&footer_size), 4);

        f.seekg(-8 - static_cast<std::streamoff>(footer_size), std::ios::end);

        uint64_t num_rows = 0;
        uint32_t num_cols = 0;
        f.read(reinterpret_cast<char*>(&num_rows), 8);
        f.read(reinterpret_cast<char*>(&num_cols), 4);

        struct ColMeta {
            std::string name;
            DataType    type;
            uint64_t    offset;
            uint64_t    size;
        };
        std::vector<ColMeta> metas;
        metas.reserve(num_cols);

        TableSchema schema;
        for (uint32_t i = 0; i < num_cols; ++i) {
            uint8_t name_len = 0;
            f.read(reinterpret_cast<char*>(&name_len), 1);
            std::string col_name(name_len, '\0');
            f.read(col_name.data(), name_len);
            uint8_t type = 0;
            f.read(reinterpret_cast<char*>(&type), 1);
            uint64_t offset = 0, size = 0;
            f.read(reinterpret_cast<char*>(&offset), 8);
            f.read(reinterpret_cast<char*>(&size),   8);

            schema.add(col_name, static_cast<DataType>(type));
            metas.push_back({col_name, static_cast<DataType>(type), offset, size});
        }

        auto table = std::make_unique<Table>(name, std::move(schema));
        table->id = id;

        for (std::size_t i = 0; i < metas.size(); ++i) {
            f.seekg(static_cast<std::streamoff>(metas[i].offset));
            read_column(f, table->columns[i], num_rows);
        }

        return table;
    }

    static void read_column(std::ifstream& f, Column& col, uint64_t num_rows) {
        std::visit([&](auto& vec) {
            using T = typename std::decay_t<decltype(vec)>::value_type;
            vec.reserve(static_cast<std::size_t>(num_rows));
            for (uint64_t i = 0; i < num_rows; ++i) {
                if constexpr (std::is_same_v<T, std::string>) {
                    uint32_t len = 0;
                    f.read(reinterpret_cast<char*>(&len), 4);
                    std::string s(len, '\0');
                    f.read(s.data(), len);
                    vec.push_back(std::move(s));
                } else if constexpr (std::is_same_v<T, bool>) {
                    uint8_t b = 0;
                    f.read(reinterpret_cast<char*>(&b), 1);
                    vec.push_back(b != 0);
                } else {
                    T val{};
                    f.read(reinterpret_cast<char*>(&val), sizeof(T));
                    vec.push_back(val);
                }
            }
        }, col.data);
    }
};

} // namespace felwood
