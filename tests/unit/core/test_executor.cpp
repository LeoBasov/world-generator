#include "wg/core/executor.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "wg/core/cancel.hpp"
#include "wg/core/rng.hpp"
#include "wg/core/thread_pool.hpp"

using wg::core::CancelFlag;
using wg::core::chunk_at;
using wg::core::chunk_count;
using wg::core::ChunkRange;
using wg::core::Executor;
using wg::core::Expected;
using wg::core::SerialExecutor;
using wg::core::StatusCode;
using wg::core::ThreadPool;

namespace {

constexpr std::int64_t kGrain = 4096;
constexpr std::array<std::int32_t, 4> kThreadCounts = {1, 2, 8, 16};

// The chunking rule is part of the frozen layout (ADR 0003).
static_assert(chunk_count(0, 0, 4) == 0);
static_assert(chunk_count(5, 3, 4) == 0);
static_assert(chunk_count(0, 1, 4) == 1);
static_assert(chunk_count(0, 8, 4) == 2);
static_assert(chunk_count(0, 9, 4) == 3);
static_assert(chunk_count(-3, 5, 2) == 4);
static_assert(chunk_at(0, 10, 4, 0) == ChunkRange{.begin = 0, .end = 4, .index = 0, .worker = 0});
static_assert(chunk_at(0, 10, 4, 2) == ChunkRange{.begin = 8, .end = 10, .index = 2, .worker = 0});
static_assert(chunk_at(-3, 5, 2, 1, 7) ==
              ChunkRange{.begin = -1, .end = 1, .index = 1, .worker = 7});

std::uint64_t bits(double x) { return std::bit_cast<std::uint64_t>(x); }

// Values over ten decades, so a float64 sum depends on the order of the additions.
std::vector<float> make_values(std::size_t n) {
  constexpr std::array<double, 6> kScale = {1e-3, 1e-1, 1e1, 1e3, 1e5, 1e7};
  const wg::core::RngKey key = wg::core::stage_key(7, "test.executor", 0);
  std::vector<float> values(n);
  for (std::size_t i = 0; i < n; ++i) {
    const double u = wg::core::uniform01(key, i, 0) - 0.5;
    values[i] = static_cast<float>(u * kScale[i % kScale.size()]);
  }
  return values;
}

double chunk_sum(const std::vector<float>& values, const ChunkRange& r) {
  double s = 0.0;
  for (std::int64_t i = r.begin; i < r.end; ++i) {
    s += values[static_cast<std::size_t>(i)];
  }
  return s;
}

// The reduction written out serially: chunk sums added in chunk order.
double reference_sum(const std::vector<float>& values) {
  const auto n = static_cast<std::int64_t>(values.size());
  double total = 0.0;
  for (std::int64_t c = 0; c < chunk_count(0, n, kGrain); ++c) {
    total += chunk_sum(values, chunk_at(0, n, kGrain, c));
  }
  return total;
}

Expected<double> parallel_sum(Executor& exec, const std::vector<float>& values) {
  const auto map = [&values](const ChunkRange& r) { return chunk_sum(values, r); };
  const auto add = [](double a, double b) { return a + b; };
  const auto n = static_cast<std::int64_t>(values.size());
  return wg::core::parallel_reduce(exec, 0, n, kGrain, 0.0, map, add);
}

// Order-sensitive reduction: any reordering of the chunk partials changes the result.
Expected<std::uint64_t> ordered_hash(Executor& exec, const std::vector<float>& values) {
  const auto map = [&values](const ChunkRange& r) { return bits(chunk_sum(values, r)); };
  const auto mix = [](std::uint64_t h, std::uint64_t w) { return wg::core::combine(h, w); };
  const auto n = static_cast<std::int64_t>(values.size());
  return wg::core::parallel_reduce(exec, 0, n, kGrain, std::uint64_t{0}, map, mix);
}

// Chunks seen by one parallel_for, by chunk index, with every index's visit count.
struct Recording {
  std::vector<ChunkRange> chunks;
  std::vector<int> visits;
  bool ok = false;
};

Recording record(Executor& exec, std::int64_t begin, std::int64_t end, std::int64_t grain) {
  Recording rec;
  rec.chunks.resize(static_cast<std::size_t>(chunk_count(begin, end, grain)));
  rec.visits.resize(static_cast<std::size_t>(end - begin), 0);
  const auto body = [&](const ChunkRange& r) {
    rec.chunks[static_cast<std::size_t>(r.index)] = r;
    for (std::int64_t i = r.begin; i < r.end; ++i) {
      ++rec.visits[static_cast<std::size_t>(i - begin)];
    }
  };
  rec.ok = wg::core::parallel_for(exec, begin, end, grain, body).ok();
  return rec;
}

}  // namespace

TEST_CASE("core.executor reductions are bit-identical for 1, 2, 8 and 16 threads") {
  const std::vector<float> values = make_values(1'000'000);
  SerialExecutor serial;
  const Expected<double> sum = parallel_sum(serial, values);
  REQUIRE(sum);
  CHECK(bits(*sum) == bits(reference_sum(values)));
  const Expected<std::uint64_t> hash = ordered_hash(serial, values);
  REQUIRE(hash);

  for (const std::int32_t threads : kThreadCounts) {
    CAPTURE(threads);
    auto pool = ThreadPool::create(threads);
    REQUIRE(pool);
    REQUIRE((*pool)->concurrency() == threads);
    for (int repeat = 0; repeat < 3; ++repeat) {
      const Expected<double> s = parallel_sum(**pool, values);
      REQUIRE(s);
      CHECK(bits(*s) == bits(*sum));
      const Expected<std::uint64_t> h = ordered_hash(**pool, values);
      REQUIRE(h);
      CHECK(*h == *hash);
    }
  }
}

TEST_CASE("core.executor chunks depend only on range and grain") {
  constexpr std::int64_t kBegin = 3;
  constexpr std::int64_t kEnd = 10'003;
  constexpr std::int64_t kSmallGrain = 97;
  SerialExecutor serial;
  CHECK(serial.concurrency() == 1);
  const Recording expected = record(serial, kBegin, kEnd, kSmallGrain);
  REQUIRE(expected.ok);
  for (std::size_t c = 0; c < expected.chunks.size(); ++c) {
    CHECK(expected.chunks[c] == chunk_at(kBegin, kEnd, kSmallGrain, static_cast<std::int64_t>(c)));
  }

  for (const std::int32_t threads : kThreadCounts) {
    CAPTURE(threads);
    auto pool = ThreadPool::create(threads);
    REQUIRE(pool);
    const Recording rec = record(**pool, kBegin, kEnd, kSmallGrain);
    REQUIRE(rec.ok);
    CHECK(std::all_of(rec.visits.begin(), rec.visits.end(), [](int v) { return v == 1; }));
    REQUIRE(rec.chunks.size() == expected.chunks.size());
    for (std::size_t c = 0; c < rec.chunks.size(); ++c) {
      CHECK(rec.chunks[c].begin == expected.chunks[c].begin);
      CHECK(rec.chunks[c].end == expected.chunks[c].end);
      CHECK(rec.chunks[c].index == expected.chunks[c].index);
      CHECK(rec.chunks[c].worker >= 0);
      CHECK(rec.chunks[c].worker < threads);
    }
  }
}

TEST_CASE("core.executor empty ranges and invalid arguments") {
  SerialExecutor serial;
  int calls = 0;
  const auto body = [&calls](const ChunkRange& /*r*/) { ++calls; };
  CHECK(wg::core::parallel_for(serial, 5, 5, 4, body).ok());
  CHECK(calls == 0);

  CHECK(wg::core::parallel_for(serial, 0, 10, 0, body).code == StatusCode::invalid_argument);
  CHECK(wg::core::parallel_for(serial, 0, 10, -1, body).code == StatusCode::invalid_argument);
  CHECK(wg::core::parallel_for(serial, 10, 0, 4, body).code == StatusCode::invalid_argument);
  CHECK(calls == 0);

  const auto map = [](const ChunkRange& r) { return r.end - r.begin; };
  const auto add = [](std::int64_t a, std::int64_t b) { return a + b; };
  const auto empty = wg::core::parallel_reduce(serial, 7, 7, 4, std::int64_t{42}, map, add);
  REQUIRE(empty);
  CHECK(*empty == 42);
  const auto bad = wg::core::parallel_reduce(serial, 0, 10, 0, std::int64_t{0}, map, add);
  REQUIRE_FALSE(bad);
  CHECK(bad.error().code == StatusCode::invalid_argument);
  const auto counted = wg::core::parallel_reduce(serial, -5, 95, 7, std::int64_t{0}, map, add);
  REQUIRE(counted);
  CHECK(*counted == 100);
}

TEST_CASE("core.executor cancel is observed within one chunk") {
  SerialExecutor serial;
  CancelFlag cancel;
  std::int64_t calls = 0;
  std::int64_t last = -1;
  const auto body = [&](const ChunkRange& r) {
    ++calls;
    last = r.index;
    if (r.index == 3) {
      cancel.request();
    }
  };
  const auto status = wg::core::parallel_for(serial, 0, 100, 10, body, &cancel);
  CHECK(status.code == StatusCode::cancelled);
  CHECK(calls == 4);
  CHECK(last == 3);

  // A flag that is already set runs no chunk.
  calls = 0;
  CHECK(wg::core::parallel_for(serial, 0, 100, 10, body, &cancel).code == StatusCode::cancelled);
  CHECK(calls == 0);

  // Without a request the loop completes, and the reduction propagates a cancel.
  cancel.reset();
  const auto map = [&cancel](const ChunkRange& r) {
    if (r.index == 1) {
      cancel.request();
    }
    return r.end - r.begin;
  };
  const auto add = [](std::int64_t a, std::int64_t b) { return a + b; };
  const auto reduced =
      wg::core::parallel_reduce(serial, 0, 100, 10, std::int64_t{0}, map, add, &cancel);
  REQUIRE_FALSE(reduced);
  CHECK(reduced.error().code == StatusCode::cancelled);

  cancel.reset();
  int plain_calls = 0;
  const auto plain = [&plain_calls](const ChunkRange& /*r*/) { ++plain_calls; };
  CHECK(wg::core::parallel_for(serial, 0, 100, 10, plain, &cancel).ok());
  CHECK(plain_calls == 10);
}
