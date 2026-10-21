#include "wg/core/executor.hpp"

#include <cstdint>

#include "wg/core/assert.hpp"
#include "wg/core/cancel.hpp"
#include "wg/core/status.hpp"

namespace wg::core {

void SerialExecutor::parallel_for(std::int64_t begin, std::int64_t end, std::int64_t grain,
                                  ChunkBody body) noexcept {
  WG_ASSERT(grain > 0 && begin <= end, "parallel_for needs grain > 0 and begin <= end");
  const std::int64_t chunks = chunk_count(begin, end, grain);
  for (std::int64_t c = 0; c < chunks; ++c) {
    body(chunk_at(begin, end, grain, c));
  }
}

Status check_chunked_range(std::int64_t begin, std::int64_t end, std::int64_t grain) noexcept {
  if (grain <= 0) {
    return make_status(StatusCode::invalid_argument, "parallel_for: grain must be positive");
  }
  if (begin > end) {
    return make_status(StatusCode::invalid_argument, "parallel_for: begin must not exceed end");
  }
  return {};
}

Status parallel_for(Executor& exec, std::int64_t begin, std::int64_t end, std::int64_t grain,
                    ChunkBody body, const CancelFlag* cancel) noexcept {
  Status valid = check_chunked_range(begin, end, grain);
  if (!valid.ok()) {
    return valid;
  }
  if (cancel == nullptr) {
    exec.parallel_for(begin, end, grain, body);
    return {};
  }
  if (cancel->requested()) {
    return make_status(StatusCode::cancelled, "parallel_for: cancel requested");
  }
  const auto guarded = [&](const ChunkRange& r) {
    if (!cancel->requested()) {
      body(r);
    }
  };
  exec.parallel_for(begin, end, grain, guarded);
  if (cancel->requested()) {
    return make_status(StatusCode::cancelled, "parallel_for: cancel requested");
  }
  return {};
}

}  // namespace wg::core
