#include "wg/core/field_store.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "support/counting_resource.hpp"

using wg::core::FieldId;
using wg::core::FieldStore;
using wg::core::StatusCode;

namespace {

constexpr std::int64_t kCells = 25;
constexpr std::size_t kN = 25;  // kCells as a size

}  // namespace

TEST_CASE("core.field_store sizes fields by dtype") {
  const FieldStore store(kCells);
  CHECK(store.cells() == kCells);
  CHECK(store.field_bytes(FieldId::elevation) == kN * 4);  // f32
  CHECK(store.field_bytes(FieldId::water) == kN * 1);      // u8
  CHECK(store.field_bytes(FieldId::biome) == kN * 2);      // u16
}

TEST_CASE("core.field_store get of an absent field is not_found") {
  const FieldStore store(kCells);
  CHECK_FALSE(store.has(FieldId::elevation));
  const auto r = store.get(FieldId::elevation);
  REQUIRE_FALSE(r);
  CHECK(r.error().code == StatusCode::not_found);
  CHECK(r.error().message.find("elevation") != std::string::npos);
}

TEST_CASE("core.field_store ensure allocates once, aligned and zeroed") {
  wg::test::CountingResource counter;
  FieldStore store(kCells, counter);
  const auto a = store.ensure(FieldId::rainfall);
  REQUIRE(a);
  CHECK(a->size() == store.field_bytes(FieldId::rainfall));
  CHECK(reinterpret_cast<std::uintptr_t>(a->data()) % FieldStore::kAlignment == 0);
  CHECK(counter.last_alignment == FieldStore::kAlignment);
  CHECK(std::all_of(a->begin(), a->end(), [](std::byte b) { return b == std::byte{0}; }));
  CHECK(store.has(FieldId::rainfall));
  CHECK_FALSE(store.is_adopted(FieldId::rainfall));

  const auto b = store.ensure(FieldId::rainfall);
  REQUIRE(b);
  CHECK(b->data() == a->data());
  CHECK(counter.allocations == 1);
  CHECK(store.bytes_owned() == counter.bytes_outstanding);

  const auto g = store.get(FieldId::rainfall);
  REQUIRE(g);
  CHECK(g->data() == a->data());
}

TEST_CASE("core.field_store ensure reports a failed allocation") {
  wg::test::CountingResource counter;
  counter.fail = true;
  FieldStore store(kCells, counter);
  const auto r = store.ensure(FieldId::elevation);
  REQUIRE_FALSE(r);
  CHECK(r.error().code == StatusCode::out_of_memory);
  CHECK_FALSE(store.has(FieldId::elevation));
  CHECK(store.bytes_owned() == 0);
}

TEST_CASE("core.field_store release and clear return memory") {
  wg::test::CountingResource counter;
  {
    FieldStore store(kCells, counter);
    REQUIRE(store.ensure(FieldId::elevation));
    REQUIRE(store.ensure(FieldId::water));
    CHECK(counter.bytes_outstanding == kN * 5);
    store.release(FieldId::elevation);
    CHECK(counter.bytes_outstanding == kN * 1);
    CHECK(store.bytes_owned() == kN * 1);
    store.release(FieldId::elevation);  // no-op when absent
    CHECK(counter.deallocations == 1);
    REQUIRE(store.ensure(FieldId::biome));
  }
  CHECK(counter.bytes_outstanding == 0);
  CHECK(counter.allocations == counter.deallocations);
}

TEST_CASE("core.field_store adopt validates size and alignment") {
  FieldStore store(kCells);
  std::vector<float> ok(kN);
  std::vector<float> small(kN - 1);
  const auto small_bytes = std::as_writable_bytes(std::span(small));
  CHECK(store.adopt(FieldId::elevation, small_bytes).code == StatusCode::invalid_argument);
  CHECK(store.adopt(FieldId::elevation, {}).code == StatusCode::invalid_argument);

  // Same byte count, shifted by one byte: misaligned for f32.
  std::vector<std::byte> raw(store.field_bytes(FieldId::elevation) + 4);
  std::byte* base = raw.data();
  if (reinterpret_cast<std::uintptr_t>(base) % 4 == 0) {
    ++base;
  }
  const std::span<std::byte> shifted(base, store.field_bytes(FieldId::elevation));
  CHECK(store.adopt(FieldId::elevation, shifted).code == StatusCode::invalid_argument);
  CHECK_FALSE(store.has(FieldId::elevation));

  CHECK(store.adopt(FieldId::elevation, std::as_writable_bytes(std::span(ok))).ok());
  CHECK(store.is_adopted(FieldId::elevation));
}

TEST_CASE("core.field_store adopt replaces and frees owned storage") {
  wg::test::CountingResource counter;
  FieldStore store(kCells, counter);
  REQUIRE(store.ensure(FieldId::elevation));
  std::vector<float> external(kN);
  REQUIRE(store.adopt(FieldId::elevation, std::as_writable_bytes(std::span(external))).ok());
  CHECK(counter.bytes_outstanding == 0);
  CHECK(store.bytes_owned() == 0);
  const auto g = store.get(FieldId::elevation);
  REQUIRE(g);
  CHECK(static_cast<const void*>(g->data()) == static_cast<const void*>(external.data()));

  store.release(FieldId::elevation);
  CHECK_FALSE(store.has(FieldId::elevation));
  CHECK(counter.deallocations == 1);  // only the owned buffer
}

TEST_CASE("core.field_store move transfers ownership") {
  wg::test::CountingResource counter;
  FieldStore a(kCells, counter);
  const auto p = a.ensure(FieldId::elevation);
  REQUIRE(p);
  FieldStore b(std::move(a));
  CHECK(b.has(FieldId::elevation));
  CHECK(b.bytes_owned() == counter.bytes_outstanding);

  FieldStore c(kCells, counter);
  REQUIRE(c.ensure(FieldId::water));
  c = std::move(b);
  CHECK(c.has(FieldId::elevation));
  CHECK_FALSE(c.has(FieldId::water));
  const auto g = c.get(FieldId::elevation);
  REQUIRE(g);
  CHECK(g->data() == p->data());
  CHECK(counter.bytes_outstanding == c.bytes_owned());
}
