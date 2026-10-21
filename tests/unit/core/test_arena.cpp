#include "wg/core/arena.hpp"

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>

#include "support/counting_resource.hpp"

using wg::core::Arena;
using wg::core::ArenaScope;
using wg::core::StatusCode;

namespace {

bool aligned(const void* p) { return reinterpret_cast<std::uintptr_t>(p) % Arena::kAlignment == 0; }

}  // namespace

TEST_CASE("core.arena create rejects zero capacity and reports failed allocation") {
  const auto zero = Arena::create(0);
  REQUIRE_FALSE(zero);
  CHECK(zero.error().code == StatusCode::invalid_argument);

  wg::test::CountingResource counter;
  counter.fail = true;
  const auto failed = Arena::create(1024, counter);
  REQUIRE_FALSE(failed);
  CHECK(failed.error().code == StatusCode::out_of_memory);
}

TEST_CASE("core.arena allocations are aligned and zeroed, from one upstream block") {
  wg::test::CountingResource counter;
  {
    auto arena = Arena::create(4096, counter);
    REQUIRE(arena);
    CHECK(arena->capacity() == 4096);
    CHECK(counter.allocations == 1);

    const auto a = arena->allocate<float>(10);
    REQUIRE(a);
    CHECK(a->size() == 10);
    CHECK(aligned(a->data()));
    const auto b = arena->allocate<std::uint8_t>(3);
    REQUIRE(b);
    CHECK(aligned(b->data()));
    const auto* a_end = reinterpret_cast<const std::byte*>(a->data() + 10);
    CHECK(reinterpret_cast<const std::byte*>(b->data()) >= a_end);
    for (const float v : *a) {
      CHECK(v == 0.0F);
    }
    (*a)[0] = 1.0F;
    CHECK(counter.allocations == 1);

    const auto empty = arena->allocate<double>(0);
    REQUIRE(empty);
    CHECK(empty->empty());
  }
  CHECK(counter.bytes_outstanding == 0);
}

TEST_CASE("core.arena exhaustion is out_of_memory") {
  auto arena = Arena::create(256);
  REQUIRE(arena);
  REQUIRE(arena->allocate<std::uint8_t>(200));
  const auto r = arena->allocate<std::uint8_t>(100);
  REQUIRE_FALSE(r);
  CHECK(r.error().code == StatusCode::out_of_memory);

  const auto huge = arena->allocate<double>(std::numeric_limits<std::size_t>::max() / 4);
  REQUIRE_FALSE(huge);
  CHECK(huge.error().code == StatusCode::out_of_memory);

  arena->reset();
  CHECK(arena->used() == 0);
  CHECK(arena->allocate<std::uint8_t>(256));
}

TEST_CASE("core.arena mark and rewind reuse memory and re-zero it") {
  auto arena = Arena::create(1024);
  REQUIRE(arena);
  const Arena::Marker m = arena->mark();
  const auto a = arena->allocate<std::int32_t>(8);
  REQUIRE(a);
  (*a)[3] = 99;
  arena->rewind(m);
  CHECK(arena->used() == m.offset);
  const auto b = arena->allocate<std::int32_t>(8);
  REQUIRE(b);
  CHECK(b->data() == a->data());
  CHECK((*b)[3] == 0);
}

TEST_CASE("core.arena scope restores the position") {
  auto arena = Arena::create(1024);
  REQUIRE(arena);
  REQUIRE(arena->allocate<float>(4));
  const std::size_t before = arena->used();
  {
    const ArenaScope scope(*arena);
    REQUIRE(arena->allocate<float>(64));
    CHECK(arena->used() > before);
  }
  CHECK(arena->used() == before);
}

TEST_CASE("core.arena move transfers the block") {
  wg::test::CountingResource counter;
  {
    auto a = Arena::create(512, counter);
    REQUIRE(a);
    REQUIRE(a->allocate<float>(4));
    Arena b = std::move(*a);
    CHECK(b.capacity() == 512);
    CHECK(b.used() > 0);

    auto c = Arena::create(128, counter);
    REQUIRE(c);
    *c = std::move(b);
    CHECK(c->capacity() == 512);
    CHECK(counter.deallocations == 1);  // c's own block
  }
  CHECK(counter.bytes_outstanding == 0);
  CHECK(counter.allocations == counter.deallocations);
}
