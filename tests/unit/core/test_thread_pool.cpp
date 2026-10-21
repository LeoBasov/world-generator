#include "wg/core/thread_pool.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <vector>

#include "wg/core/cancel.hpp"
#include "wg/core/executor.hpp"

using wg::core::CancelFlag;
using wg::core::ChunkRange;
using wg::core::Expected;
using wg::core::StatusCode;
using wg::core::ThreadPool;

namespace {

// Sum of the indices of [0, n) through the pool.
Expected<std::int64_t> index_sum(ThreadPool& pool, std::int64_t n, std::int64_t grain) {
  const auto map = [](const ChunkRange& r) {
    std::int64_t s = 0;
    for (std::int64_t i = r.begin; i < r.end; ++i) {
      s += i;
    }
    return s;
  };
  const auto add = [](std::int64_t a, std::int64_t b) { return a + b; };
  return wg::core::parallel_reduce(pool, 0, n, grain, std::int64_t{0}, map, add);
}

}  // namespace

TEST_CASE("core.thread_pool create validates the thread count") {
  const auto negative = ThreadPool::create(-1);
  REQUIRE_FALSE(negative);
  CHECK(negative.error().code == StatusCode::invalid_argument);
  const auto too_many = ThreadPool::create(ThreadPool::kMaxThreads + 1);
  REQUIRE_FALSE(too_many);
  CHECK(too_many.error().code == StatusCode::invalid_argument);

  const auto hardware = ThreadPool::create(0);
  REQUIRE(hardware);
  const unsigned hw = std::thread::hardware_concurrency();
  const std::int32_t expected = hw == 0 ? 1 : static_cast<std::int32_t>(std::min(hw, 256U));
  CHECK((*hardware)->concurrency() == expected);

  const auto four = ThreadPool::create(4);
  REQUIRE(four);
  CHECK((*four)->concurrency() == 4);
}

TEST_CASE("core.thread_pool a single thread runs everything on worker 0") {
  auto pool = ThreadPool::create(1);
  REQUIRE(pool);
  CHECK((*pool)->concurrency() == 1);
  std::int64_t chunks = 0;
  bool all_worker_zero = true;
  const auto body = [&](const ChunkRange& r) {
    ++chunks;
    all_worker_zero = all_worker_zero && r.worker == 0;
  };
  CHECK(wg::core::parallel_for(**pool, 0, 1000, 10, body).ok());
  CHECK(chunks == 100);
  CHECK(all_worker_zero);
}

TEST_CASE("core.thread_pool per-worker state needs no synchronisation") {
  auto pool = ThreadPool::create(8);
  REQUIRE(pool);
  ThreadPool& p = **pool;
  // One accumulator per worker, as a kernel keeps one Arena per worker. TSan reports a race if
  // two chunks ran on the same worker index at the same time.
  std::vector<std::int64_t> per_worker(static_cast<std::size_t>(p.concurrency()), 0);
  const auto body = [&](const ChunkRange& r) {
    if (r.worker >= 0 && r.worker < p.concurrency()) {  // else the total below comes up short
      per_worker[static_cast<std::size_t>(r.worker)] += r.end - r.begin;
    }
  };
  for (int repeat = 0; repeat < 20; ++repeat) {
    std::fill(per_worker.begin(), per_worker.end(), 0);
    REQUIRE(wg::core::parallel_for(p, 0, 100'000, 64, body).ok());
    std::int64_t total = 0;
    for (const std::int64_t v : per_worker) {
      total += v;
    }
    CHECK(total == 100'000);
  }
}

TEST_CASE("core.thread_pool nested parallel_for runs inline on the same worker") {
  auto pool = ThreadPool::create(4);
  REQUIRE(pool);
  ThreadPool& p = **pool;
  constexpr std::int64_t kRows = 16;
  constexpr std::int64_t kCols = 64;
  std::vector<std::int64_t> cells(static_cast<std::size_t>(kRows * kCols), -1);
  std::vector<std::uint8_t> row_ok(static_cast<std::size_t>(kRows), 0);
  const auto row_body = [&](const ChunkRange& row) {
    bool same_worker = true;
    const auto col_body = [&](const ChunkRange& col) {
      same_worker = same_worker && col.worker == row.worker;
      for (std::int64_t c = col.begin; c < col.end; ++c) {
        cells[static_cast<std::size_t>((row.begin * kCols) + c)] = row.begin;
      }
    };
    const bool inner_ok = wg::core::parallel_for(p, 0, kCols, 8, col_body).ok();
    row_ok[static_cast<std::size_t>(row.begin)] = inner_ok && same_worker ? 1 : 0;
  };
  REQUIRE(wg::core::parallel_for(p, 0, kRows, 1, row_body).ok());
  CHECK(std::all_of(row_ok.begin(), row_ok.end(), [](std::uint8_t v) { return v == 1; }));
  for (std::int64_t i = 0; i < kRows * kCols; ++i) {
    CHECK(cells[static_cast<std::size_t>(i)] == i / kCols);
  }
}

TEST_CASE("core.thread_pool calls from several threads are serialised") {
  auto pool = ThreadPool::create(4);
  REQUIRE(pool);
  ThreadPool& p = **pool;
  constexpr std::int64_t kN = 10'000;
  constexpr std::int64_t kExpected = kN * (kN - 1) / 2;
  std::array<bool, 2> all_ok = {false, false};
  const auto submit = [&](std::size_t slot) {
    bool ok = true;
    for (int k = 0; k < 50; ++k) {
      const auto r = index_sum(p, kN, 97);
      ok = ok && r && *r == kExpected;
    }
    all_ok.at(slot) = ok;
  };
  std::thread a(submit, std::size_t{0});
  std::thread b(submit, std::size_t{1});
  a.join();
  b.join();
  CHECK(all_ok[0]);
  CHECK(all_ok[1]);
}

TEST_CASE("core.thread_pool stress: many small jobs") {
  auto pool = ThreadPool::create(8);
  REQUIRE(pool);
  ThreadPool& p = **pool;
  int failures = 0;
  for (std::int64_t job = 0; job < 2000; ++job) {
    const std::int64_t n = 1 + (job % 61);
    const auto r = index_sum(p, n, 1 + (job % 5));
    if (!r || *r != n * (n - 1) / 2) {
      ++failures;
    }
  }
  CHECK(failures == 0);
}

TEST_CASE("core.thread_pool cancel is observed within one chunk per worker") {
  constexpr std::int32_t kThreads = 4;
  constexpr std::int64_t kChunks = 100'000;
  auto pool = ThreadPool::create(kThreads);
  REQUIRE(pool);
  CancelFlag cancel;
  std::atomic<std::int64_t> ran{0};
  std::atomic<std::int64_t> started_after_request{0};
  const auto body = [&](const ChunkRange& r) {
    if (cancel.requested()) {
      started_after_request.fetch_add(1, std::memory_order_relaxed);
    }
    ran.fetch_add(1, std::memory_order_relaxed);
    if (r.index == 10) {
      cancel.request();
    }
  };
  const auto status = wg::core::parallel_for(**pool, 0, kChunks, 1, body, &cancel);
  CHECK(status.code == StatusCode::cancelled);
  // Every chunk checks the flag before its body, so a worker runs at most one body after the
  // request (one it had already passed the check for); the requesting worker runs none.
  CHECK(started_after_request.load() <= kThreads - 1);
  CHECK(ran.load() < kChunks);
}

TEST_CASE("core.thread_pool destruction of an idle pool joins its threads") {
  for (int k = 0; k < 20; ++k) {
    auto pool = ThreadPool::create(8);
    REQUIRE(pool);
    if (k % 2 == 0) {
      const auto r = index_sum(**pool, 1000, 10);
      REQUIRE(r);
      CHECK(*r == 499'500);
    }
  }
}
