#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// felwood::Operator – Volcano / iterator model base class
//
// What this does:
//   Implements the classic Volcano (or "iterator") model introduced by Graefe
//   (1994).  Every operator exposes three calls:
//     open()  – initialise internal state, recurse to child operators.
//     next()  – produce the next Chunk (batch of rows); nullopt == exhausted.
//     close() – release resources, recurse to child operators.
//
//   Callers drive the pipeline by looping:
//     op->open();
//     while (auto chunk = op->next()) { … }
//     op->close();
//
// Where a real engine diverges:
//   • Push / producer model (Hyper, Velox): data is pushed upward; avoids
//     virtual calls in the hot loop, enables better register reuse.
//   • Morsel-driven parallelism (Leis et al. 2014): a thread pool steals
//     "morsels" (sub-ranges of input) and executes pipelines in parallel.
//   • Pipeline breakers (hash-join build, sort, hash-agg) materialise their
//     entire input before producing any output; they are scheduled separately.
//   • JIT / codegen (LLVM or template metaprogramming): operators are fused
//     into a single tight loop per pipeline, eliminating virtual dispatch and
//     intermediate materialization entirely.
//   • Adaptive execution: the runtime can switch between interpreted and
//     compiled execution based on observed cardinalities.
//
// What to improve next:
//   • Add an ExpressionTree (AST) that operators compile to lambdas or LLVM IR.
//   • Add OperatorStats (rows in/out, elapsed time) for EXPLAIN ANALYZE.
//   • Introduce a Pipeline class that groups operators between breakers and
//     schedules them as a unit across worker threads.
// ─────────────────────────────────────────────────────────────────────────────

#include "common/column.hpp"

#include <optional>

namespace felwood {

class Operator {
public:
    virtual ~Operator() = default;

    // Initialise state and recurse to child(ren).
    virtual void open() = 0;

    // Return the next output Chunk, or std::nullopt when exhausted.
    // The caller must not call next() again after nullopt is returned.
    virtual std::optional<Chunk> next() = 0;

    // Release any resources held by this operator and its subtree.
    virtual void close() = 0;
};

} // namespace felwood
