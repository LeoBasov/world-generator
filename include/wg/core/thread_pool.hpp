#pragma once

#include <cstdint>
#include <memory>

#include "wg/core/executor.hpp"
#include "wg/core/expected.hpp"

namespace wg::core {

/// The library's internal executor (plan section 6.4), used by the CLI and the tests. A pool of
/// T workers: the thread calling parallel_for is worker 0 and T - 1 pool threads are workers
/// 1 ... T - 1. Chunks are handed out in index order to whichever worker is free; determinism
/// comes from the fixed chunking (executor.hpp), not from the schedule. A parallel_for from
/// inside a body runs inline on the calling worker. Calls from different threads are
/// serialised; give every concurrent run its own pool (plan section 6.6).
class ThreadPool final : public Executor {
 public:
  /// Largest accepted thread count.
  static constexpr std::int32_t kMaxThreads = 256;

  /// Pool with threads workers (including the caller), or one per hardware thread if 0.
  /// invalid_argument outside [0, kMaxThreads]; out_of_memory if a thread cannot be started.
  [[nodiscard]] static Expected<std::unique_ptr<ThreadPool>> create(
      std::int32_t threads = 0) noexcept;

  /// Stops and joins the pool threads. No parallel_for may be running.
  ~ThreadPool() override;
  ThreadPool(const ThreadPool&) = delete;
  ThreadPool& operator=(const ThreadPool&) = delete;
  ThreadPool(ThreadPool&&) = delete;
  ThreadPool& operator=(ThreadPool&&) = delete;

  [[nodiscard]] std::int32_t concurrency() const noexcept override;
  void parallel_for(std::int64_t begin, std::int64_t end, std::int64_t grain,
                    ChunkBody body) noexcept override;

 private:
  struct Impl;

  ThreadPool();

  std::unique_ptr<Impl> impl_;
};

}  // namespace wg::core
