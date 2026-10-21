# ADR 0003: Fixed chunking, ordered reductions and per-chunk cancellation

- Status: accepted
- Date: 2026-10-21
- Work package: A05

## Context

Requirement R1 asks for bit-identical worlds for any thread count. Plan section 6.3 states the
rule for parallel code: reductions use fixed chunk boundaries and combine partial results in chunk
order. Floating-point addition is not associative, so a sum whose grouping depends on the
scheduler or on the number of threads changes in the last bits from run to run.

Section 6.4 sketches an `Executor` with `parallel_for(begin, end, grain, body)`. The CLI and tests
use an internal pool, and Phase 2 may adapt the Godot WorkerThreadPool (section 6.6). The pipeline
must also stop within one stage or one kernel chunk when cancelled (section 6.4, runner
behaviour). The A05 acceptance tests are: reductions bit-identical for 1, 2, 8 and 16 threads,
TSan clean, and cancel observed within one chunk.

## Decision

The chunking of a loop is a pure function of `(begin, end, grain)`. Results are combined in chunk
order on the calling thread. Cancellation is checked before every chunk, by the library rather
than by each executor.

- **Chunking.** Chunk `c` of `[begin, end)` is `[lo, min(end, lo + grain))` with
  `lo = begin + c * grain`, and there are `ceil((end - begin) / grain)` chunks (`chunk_count`,
  `chunk_at` in `include/wg/core/executor.hpp`). Every `Executor` must cut the range this way.
  The thread count only decides which worker runs a chunk, never where a chunk starts or ends.
- **Grains are constants.** A kernel chooses its grain as a constant, for example a number of
  grid rows. It never derives it from `concurrency()` or the hardware.
- **Ordered reduction.** `parallel_reduce` stores one partial result per chunk and folds them on
  the calling thread as `reduce(...reduce(identity, p_0)..., p_last)`. Partial sums use float64
  (section 6.3).
- **Scheduling.** Chunks are claimed dynamically, in index order, by whichever worker is free.
  This affects only timing. Each chunk writes only its own outputs or its own partial.
- **Cancellation.** `CancelFlag` wraps an atomic bool with release/acquire ordering. The free
  `parallel_for` wraps the body so that each chunk checks the flag before it runs and is skipped
  once a cancel is requested, then returns `cancelled`. A worker therefore finishes at most the
  chunk it is already in, and any executor gets this behaviour, including a future Godot
  adapter. `RunContext` (A08) holds a `const CancelFlag&` instead of the
  `const std::atomic<bool>&` of the section 6.4 sketch.
- **Workers.** `ChunkRange::worker` is in `[0, concurrency())`. No two chunks of one call run on
  the same worker at the same time, so kernels can keep one scratch `Arena` per worker. In
  `ThreadPool`, the calling thread is worker 0.
- **Nesting.** A `parallel_for` on a `ThreadPool` from inside one of its own chunks runs inline
  on the calling worker, with that worker's index, instead of deadlocking. Calls from different
  external threads on one pool are serialised. Concurrent runs should each have their own pool
  (section 6.6).

## Consequences

- Results of reductions are bit-identical for every executor and thread count. The A05 tests
  check this for a serial executor and for pools of 1, 2, 8 and 16 threads, with both a float64
  sum and an order-sensitive hash of the partials.
- Changing the grain of a kernel that reduces changes its results and the golden hashes of every
  stage that uses it, so it is an intended output change that needs a golden update (`golden/**`
  branch). Changing the grain of a loop whose chunks only write disjoint outputs changes nothing.
- A cancel takes effect within one chunk per worker. Kernels with long chunks may also poll
  `CancelFlag::requested()` inside a chunk.
- Very small grains cost scheduling overhead (one atomic increment per chunk). Kernels should use
  grains of at least a few thousand cells or one or more rows.

## Alternatives considered

- **Split the range into one block per thread.** Simple, but the grouping of a sum then depends
  on the thread count, which breaks R1.
- **Tree reduction across workers.** Deterministic only if the tree shape is fixed by the chunk
  count. A left fold over at most a few thousand partials is cheap and simpler to reason about.
- **Let each executor handle cancellation.** This duplicates the logic in every implementation,
  including the Phase 2 adapter, and cannot be tested once for all of them.
- **A bare `std::atomic<bool>&` as in the sketch.** It works, but leaves the memory order to each
  call site and exposes `store` with any value. The wrapper names the three operations.
- **`std::function` for the body.** It may allocate and is not trivially copyable. `FunctionRef`
  is two pointers, and the body only has to live for the duration of the call.
