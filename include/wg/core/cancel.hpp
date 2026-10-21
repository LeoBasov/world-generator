#pragma once

#include <atomic>

namespace wg::core {

/// Cooperative cancellation (plan sections 6.4 and 6.6). The owner, e.g. the CLI's signal
/// handler or the Godot binding, calls request() from any thread; the pipeline polls it between
/// stages and parallel_for before every chunk, and long kernels may poll it inside a chunk.
/// Neither copyable nor movable: share it by reference.
class CancelFlag {
 public:
  CancelFlag() noexcept = default;

  /// Asks the running work to stop. Thread-safe, idempotent.
  void request() noexcept { flag_.store(true, std::memory_order_release); }

  /// True once request() was called and until reset(). Cheap enough to poll per row.
  [[nodiscard]] bool requested() const noexcept { return flag_.load(std::memory_order_acquire); }

  /// Clears the request before the next run. Not to be called while work polls the flag.
  void reset() noexcept { flag_.store(false, std::memory_order_release); }

 private:
  std::atomic<bool> flag_{false};
};

}  // namespace wg::core
