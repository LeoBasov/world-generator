#include "wg/core/world_state.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "support/counting_resource.hpp"

using wg::core::FieldId;
using wg::core::FieldView;
using wg::core::Grid;
using wg::core::StatusCode;
using wg::core::WorldState;

namespace {

constexpr Grid kGrid{.n = 5, .dx_m = 1000.0, .hu_m = 10.0};

WorldState make_state(wg::core::MemoryResource& r = wg::core::default_memory_resource()) {
  auto s = WorldState::create(kGrid, r);
  REQUIRE(s);
  return std::move(*s);
}

}  // namespace

TEST_CASE("core.world_state create validates the grid") {
  const auto bad = WorldState::create(Grid{.n = 4, .dx_m = 1.0, .hu_m = 1.0});
  REQUIRE_FALSE(bad);
  CHECK(bad.error().code == StatusCode::invalid_argument);

  const WorldState ws = make_state();
  CHECK(ws.grid() == kGrid);
  CHECK(ws.bytes_owned() == 0);
  CHECK(ws.layers().empty());
  CHECK(ws.provenance().library_version.major == wg::core::version().major);
  CHECK(ws.provenance().library_version.minor == wg::core::version().minor);
}

TEST_CASE("core.world_state reading an absent field returns not_found") {
  const WorldState ws = make_state();
  CHECK_FALSE(ws.has(FieldId::elevation));
  const auto r = ws.read<float>(FieldId::elevation);
  REQUIRE_FALSE(r);
  CHECK(r.error().code == StatusCode::not_found);
  const auto t = ws.read<FieldId::water>();
  REQUIRE_FALSE(t);
  CHECK(t.error().code == StatusCode::not_found);
}

TEST_CASE("core.world_state refuses a wrong dtype") {
  WorldState ws = make_state();
  const auto w = ws.write<std::uint8_t>(FieldId::elevation);
  REQUIRE_FALSE(w);
  CHECK(w.error().code == StatusCode::type_mismatch);
  CHECK(w.error().message.find("f32") != std::string::npos);
  CHECK_FALSE(ws.has(FieldId::elevation));

  REQUIRE(ws.write<float>(FieldId::elevation));
  const auto r = ws.read<std::int32_t>(FieldId::elevation);
  REQUIRE_FALSE(r);
  CHECK(r.error().code == StatusCode::type_mismatch);

  std::vector<std::int32_t> external(static_cast<std::size_t>(kGrid.cells()));
  CHECK(ws.adopt(FieldId::rainfall, std::span(external)).code == StatusCode::type_mismatch);
}

TEST_CASE("core.world_state write allocates zeroed storage once and read sees it") {
  WorldState ws = make_state();
  auto w = ws.write<FieldId::elevation>();
  REQUIRE(w);
  static_assert(std::is_same_v<std::remove_cvref_t<decltype(*w)>, FieldView<float>>);
  CHECK(w->size() == kGrid.cells());
  CHECK(w->grid == kGrid);
  for (std::int64_t i = 0; i < w->size(); ++i) {
    REQUIRE((*w)[i] == 0.0F);
  }
  (*w)(3, 2) = 150.0F;

  const auto again = ws.write<float>(FieldId::elevation);
  REQUIRE(again);
  CHECK(again->data.data() == w->data.data());

  const auto r = ws.read<FieldId::elevation>();
  REQUIRE(r);
  static_assert(std::is_same_v<std::remove_cvref_t<decltype(*r)>, FieldView<const float>>);
  CHECK((*r)(3, 2) == 150.0F);
  CHECK(r->data.data() == w->data.data());
}

TEST_CASE("core.world_state adopted buffer is used without copy") {
  wg::test::CountingResource counter;
  WorldState ws = make_state(counter);
  std::vector<float> external(static_cast<std::size_t>(kGrid.cells()), 1.0F);

  REQUIRE(ws.adopt(FieldId::temperature, std::span(external)).ok());
  CHECK(ws.has(FieldId::temperature));
  CHECK(ws.is_adopted(FieldId::temperature));

  const auto w = ws.write<float>(FieldId::temperature);
  REQUIRE(w);
  CHECK(w->data.data() == external.data());
  (*w)(1, 1) = 42.0F;
  CHECK(external[static_cast<std::size_t>(kGrid.idx(1, 1))] == 42.0F);

  const auto r = ws.read<float>(FieldId::temperature);
  REQUIRE(r);
  CHECK(r->data.data() == external.data());
  CHECK((*r)(0, 0) == 1.0F);
  CHECK(counter.allocations == 0);

  // Byte-level adopt (the binding's path) works the same way.
  std::vector<std::uint8_t> water(static_cast<std::size_t>(kGrid.cells()));
  REQUIRE(ws.adopt(FieldId::water, std::as_writable_bytes(std::span(water))).ok());
  const auto rw = ws.read<FieldId::water>();
  REQUIRE(rw);
  CHECK(static_cast<const void*>(rw->data.data()) == static_cast<const void*>(water.data()));

  // Wrong size is refused.
  std::vector<float> small(3);
  CHECK(ws.adopt(FieldId::rainfall, std::span(small)).code == StatusCode::invalid_argument);
}

TEST_CASE("core.world_state released memory is returned") {
  wg::test::CountingResource counter;
  {
    WorldState ws = make_state(counter);
    REQUIRE(ws.write<FieldId::elevation>());
    REQUIRE(ws.write<FieldId::flow_receiver>());
    const std::size_t cells = static_cast<std::size_t>(kGrid.cells());
    CHECK(counter.allocations == 2);
    CHECK(counter.bytes_outstanding == cells * 8);
    CHECK(ws.bytes_owned() == counter.bytes_outstanding);

    ws.release(FieldId::elevation);
    CHECK_FALSE(ws.has(FieldId::elevation));
    CHECK(counter.bytes_outstanding == cells * 4);
    CHECK(ws.bytes_owned() == cells * 4);

    // Releasing an adopted field forgets it without touching the resource.
    std::vector<float> external(cells);
    REQUIRE(ws.adopt(FieldId::rainfall, std::span(external)).ok());
    ws.release(FieldId::rainfall);
    CHECK_FALSE(ws.has(FieldId::rainfall));
    CHECK(counter.deallocations == 1);
  }
  CHECK(counter.bytes_outstanding == 0);
  CHECK(counter.allocations == counter.deallocations);
}

TEST_CASE("core.world_state write reports out_of_memory") {
  wg::test::CountingResource counter;
  WorldState ws = make_state(counter);
  counter.fail = true;
  const auto w = ws.write<FieldId::elevation>();
  REQUIRE_FALSE(w);
  CHECK(w.error().code == StatusCode::out_of_memory);
}

TEST_CASE("core.world_state move keeps field storage, layers and provenance") {
  WorldState a = make_state();
  const auto w = a.write<FieldId::elevation>();
  REQUIRE(w);
  a.layers().peaks.push_back({.cell = 7, .height_eu = 300.0F, .radius_cells = 2.0F});
  a.provenance().completed_stages.emplace_back("c01_allocate");

  WorldState b = std::move(a);
  const auto r = b.read<FieldId::elevation>();
  REQUIRE(r);
  CHECK(r->data.data() == w->data.data());
  CHECK(b.layers().peaks.size() == 1);
  CHECK(b.provenance().last_completed_stage() == "c01_allocate");
}
