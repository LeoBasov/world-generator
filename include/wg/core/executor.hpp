#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "wg/core/cancel.hpp"
#include "wg/core/expected.hpp"
#include "wg/core/function_ref.hpp"
#include "wg/core/status.hpp"

namespace wg::core {

// Deterministic parallel loops (plan section 6.3, requirement R1). A range [begin, end) is cut
// into chunks of grain indices; the cut depends only on (begin, end, grain), never on the thread
// count, and every reduction combines its chunk results in chunk order on the calling thread.
// So results are bit-identical for any executor and thread count. Kernels choose their grain as
// a constant (e.g. rows of the grid), never from concurrency(): changing a grain changes the
// golden hashes of every reduction that uses it (docs/adr/0003-deterministic-parallel-loops.md).

/// One chunk of a parallel loop.
struct ChunkRange {
  std::int64_t begin = 0;  ///< first index
  std::int64_t end = 0;    ///< one past the last index
  std::int64_t index = 0;  ///< chunk number, 0 for the chunk starting at the loop's begin
  /// Worker running the chunk, in [0, concurrency()); no two chunks of one call run on the same
  /// worker at the same time, so a kernel may keep per-worker scratch (one Arena per worker).
  std::int32_t worker = 0;

  friend bool operator==(const ChunkRange&, const ChunkRange&) = default;
};

/// Body of a parallel loop, called once per chunk. Must not throw.
using ChunkBody = FunctionRef<void(const ChunkRange&)>;

/// Number of chunks of [begin, end) with grain > 0; 0 for an empty range.
constexpr std::int64_t chunk_count(std::int64_t begin, std::int64_t end,
                                   std::int64_t grain) noexcept {
  return end <= begin ? 0 : (end - begin - 1) / grain + 1;
}

/// Chunk number index of [begin, end): [lo, min(end, lo + grain)) with lo = begin + index * grain.
constexpr ChunkRange chunk_at(std::int64_t begin, std::int64_t end, std::int64_t grain,
                              std::int64_t index, std::int32_t worker = 0) noexcept {
  const std::int64_t lo = begin + index * grain;
  const std::int64_t hi = end - lo <= grain ? end : lo + grain;
  return {.begin = lo, .end = hi, .index = index, .worker = worker};
}

/// Runs parallel loops (plan section 6.4). The library ships SerialExecutor and ThreadPool;
/// Phase 2 may adapt the Godot WorkerThreadPool. Kernels call the free parallel_for and
/// parallel_reduce below, which validate the range and handle cancellation.
class Executor {
 public:
  virtual ~Executor() = default;

  /// Number of workers; ChunkRange::worker is below it.
  [[nodiscard]] virtual std::int32_t concurrency() const noexcept = 0;

  /// Calls body for every chunk of [begin, end) as cut by chunk_at, in any order and on any
  /// worker, and returns once all have finished; their writes are then visible to the caller.
  /// Requires grain > 0 and begin <= end. A call from inside a body must not deadlock.
  virtual void parallel_for(std::int64_t begin, std::int64_t end, std::int64_t grain,
                            ChunkBody body) noexcept = 0;

 protected:
  Executor() = default;
  Executor(const Executor&) = default;
  Executor& operator=(const Executor&) = default;
  Executor(Executor&&) = default;
  Executor& operator=(Executor&&) = default;
};

/// Runs every chunk on the calling thread in chunk order, as worker 0.
class SerialExecutor final : public Executor {
 public:
  [[nodiscard]] std::int32_t concurrency() const noexcept override { return 1; }
  void parallel_for(std::int64_t begin, std::int64_t end, std::int64_t grain,
                    ChunkBody body) noexcept override;
};

/// ok, or invalid_argument unless grain > 0 and begin <= end.
[[nodiscard]] Status check_chunked_range(std::int64_t begin, std::int64_t end,
                                         std::int64_t grain) noexcept;

/// Parallel loop of a kernel. With a cancel flag, every chunk checks it before it starts and is
/// skipped once a cancel was requested, so a cancel takes effect within one chunk per worker;
/// returns cancelled if the flag was set when the loop finished. A body may poll the flag itself
/// to stop inside a long chunk. invalid_argument for a bad range (see check_chunked_range).
[[nodiscard]] Status parallel_for(Executor& exec, std::int64_t begin, std::int64_t end,
                                  std::int64_t grain, ChunkBody body,
                                  const CancelFlag* cancel = nullptr) noexcept;

/// Ordered parallel reduction: partial_c = map(chunk c) for every chunk, then
/// reduce(...reduce(reduce(identity, partial_0), partial_1)..., partial_last) on the calling
/// thread, so the result does not depend on the executor. map(const ChunkRange&) returns T;
/// reduce(T, T) returns T. Errors: those of parallel_for, out_of_memory for the partials.
template <class T, class Map, class Reduce>
[[nodiscard]] Expected<T> parallel_reduce(Executor& exec, std::int64_t begin, std::int64_t end,
                                          std::int64_t grain, T identity, const Map& map,
                                          const Reduce& reduce,
                                          const CancelFlag* cancel = nullptr) noexcept {
  Status valid = check_chunked_range(begin, end, grain);
  if (!valid.ok()) {
    return make_error(std::move(valid));
  }
  std::vector<T> partials;
  try {
    partials.resize(static_cast<std::size_t>(chunk_count(begin, end, grain)), identity);
  } catch (...) {
    return make_error(StatusCode::out_of_memory, "parallel_reduce: cannot allocate partials");
  }
  const auto body = [&](const ChunkRange& r) {
    partials[static_cast<std::size_t>(r.index)] = map(r);
  };
  Status status = parallel_for(exec, begin, end, grain, body, cancel);
  if (!status.ok()) {
    return make_error(std::move(status));
  }
  T acc = std::move(identity);
  for (T& partial : partials) {
    acc = reduce(std::move(acc), std::move(partial));
  }
  return Expected<T>(std::move(acc));
}

}  // namespace wg::core
