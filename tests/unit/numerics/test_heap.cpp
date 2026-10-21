#include "wg/numerics/heap.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "support/counting_resource.hpp"
#include "wg/core/rng.hpp"

using wg::core::StatusCode;
using wg::num::IndexHeap;

namespace {

using Entry = wg::num::HeapEntry<float>;

bool same(const Entry& a, const Entry& b) { return a.key == b.key && a.cell == b.cell; }

// count entries with few distinct keys, so most keys tie.
std::vector<Entry> make_entries(std::size_t count, std::uint64_t seed) {
  const wg::core::RngKey key = wg::core::stage_key(seed, "test.numerics.heap", 0);
  std::vector<Entry> entries(count);
  for (std::size_t i = 0; i < count; ++i) {
    const auto k = static_cast<float>(wg::core::uniform_index(key, i, 0, 5));
    entries[i] = Entry{k * 0.5F, static_cast<std::int64_t>(i)};
  }
  return entries;
}

// Pushes the entries in the given order and pops all of them.
std::vector<Entry> drain(const std::vector<Entry>& entries) {
  auto heap = IndexHeap<float>::create(entries.size());
  REQUIRE(heap);
  for (const Entry& e : entries) {
    REQUIRE(heap->push(e.key, e.cell).ok());
  }
  std::vector<Entry> out;
  while (!heap->empty()) {
    out.push_back(heap->pop());
  }
  return out;
}

}  // namespace

TEST_CASE("numerics.heap order is total on ties") {
  std::vector<Entry> entries = make_entries(2000, 3);
  std::vector<Entry> expected = entries;
  std::sort(expected.begin(), expected.end(), [](const Entry& a, const Entry& b) {
    return a.key < b.key || (a.key == b.key && a.cell < b.cell);
  });

  const std::vector<Entry> forward = drain(entries);
  std::reverse(entries.begin(), entries.end());
  const std::vector<Entry> backward = drain(entries);
  // A deterministic shuffle: cells sorted by an RNG draw.
  const wg::core::RngKey key = wg::core::stage_key(4, "test.numerics.heap.shuffle", 0);
  std::sort(entries.begin(), entries.end(), [&](const Entry& a, const Entry& b) {
    return wg::core::bits64(key, static_cast<std::uint64_t>(a.cell), 0) <
           wg::core::bits64(key, static_cast<std::uint64_t>(b.cell), 0);
  });
  const std::vector<Entry> shuffled = drain(entries);

  REQUIRE(forward.size() == expected.size());
  REQUIRE(backward.size() == expected.size());
  REQUIRE(shuffled.size() == expected.size());
  for (std::size_t i = 0; i < expected.size(); ++i) {
    CAPTURE(i);
    CHECK(same(forward[i], expected[i]));
    CHECK(same(backward[i], expected[i]));
    CHECK(same(shuffled[i], expected[i]));
  }
}

TEST_CASE("numerics.heap ties between signed zeros go by cell") {
  auto heap = IndexHeap<double>::create(3);
  REQUIRE(heap);
  REQUIRE(heap->push(0.0, 7).ok());
  REQUIRE(heap->push(-0.0, 9).ok());
  REQUIRE(heap->push(-1.0, 8).ok());
  CHECK(heap->pop().cell == 8);
  CHECK(heap->pop().cell == 7);
  CHECK(heap->pop().cell == 9);
}

TEST_CASE("numerics.heap has a fixed capacity") {
  CHECK(IndexHeap<std::int32_t>::create(0).error().code == StatusCode::invalid_argument);

  auto heap = IndexHeap<std::int32_t>::create(2);
  REQUIRE(heap);
  CHECK(heap->capacity() == 2);
  CHECK(heap->empty());
  REQUIRE(heap->push(5, 1).ok());
  REQUIRE(heap->push(3, 2).ok());
  CHECK(heap->push(1, 3).code == StatusCode::out_of_memory);
  CHECK(heap->size() == 2);
  CHECK(heap->top().key == 3);
  CHECK(heap->top().cell == 2);
  heap->clear();
  CHECK(heap->empty());
  REQUIRE(heap->push(1, 3).ok());
  CHECK(heap->pop().key == 1);
}

TEST_CASE("numerics.heap storage comes from the memory resource") {
  wg::test::CountingResource resource;
  {
    auto heap = IndexHeap<float>::create(100, resource);
    REQUIRE(heap);
    CHECK(resource.allocations == 1);
    IndexHeap<float> moved = std::move(*heap);
    REQUIRE(moved.push(1.0F, 4).ok());
    CHECK(moved.pop().cell == 4);
  }
  CHECK(resource.bytes_outstanding == 0);
  resource.fail = true;
  CHECK(IndexHeap<float>::create(100, resource).error().code == StatusCode::out_of_memory);
}
