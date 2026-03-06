#pragma once

#include "common/column.hpp"

#include <optional>

namespace felwood {
    class Operator {
    public:
        virtual ~Operator() = default;

        virtual void open() = 0;
        virtual std::optional<Chunk> next() = 0;
        virtual void close() = 0;
    };
}
