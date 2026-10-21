#include "wg/generated/field_ids.hpp"

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <set>
#include <string_view>
#include <type_traits>

namespace {

using wg::core::DType;
using wg::core::field_id_from_name;
using wg::core::field_info;
using wg::core::field_t;
using wg::core::FieldId;
using wg::core::kFieldCount;
using wg::core::kFields;

// The registry is usable at compile time.
static_assert(kFieldCount == kFields.size());
static_assert(kFieldCount == 28);
static_assert(field_info(FieldId::elevation).name == "elevation");
static_assert(field_id_from_name("flow_receiver") == FieldId::flow_receiver);
static_assert(!field_id_from_name("no_such_field").has_value());
static_assert(std::is_same_v<field_t<FieldId::elevation>, float>);
static_assert(std::is_same_v<field_t<FieldId::flow_receiver>, std::int32_t>);
static_assert(std::is_same_v<field_t<FieldId::salinity>, std::uint8_t>);
static_assert(std::is_same_v<field_t<FieldId::biome>, std::uint16_t>);
static_assert(std::is_same_v<std::underlying_type_t<wg::core::Water>, field_t<FieldId::water>>);

}  // namespace

TEST_CASE("generated.field_ids are dense and indexed by id") {
  for (std::size_t i = 0; i < kFields.size(); ++i) {
    CHECK(static_cast<std::size_t>(kFields[i].id) == i);
    CHECK(&field_info(kFields[i].id) == &kFields[i]);
  }
}

TEST_CASE("generated.field_ids names are unique and round-trip") {
  std::set<std::string_view> names;
  for (const auto& f : kFields) {
    CHECK(names.insert(f.name).second);
    CHECK(f.name.size() <= 32);  // world files store names in 32-byte slots
    CHECK((field_id_from_name(f.name) == f.id));
  }
  CHECK_FALSE(field_id_from_name("").has_value());
  CHECK_FALSE(field_id_from_name("Elevation").has_value());
}

TEST_CASE("generated.field_ids ranges are consistent") {
  for (const auto& f : kFields) {
    CAPTURE(f.name);
    const auto& r = f.range;
    if (r.has_min && r.has_max) {
      CHECK(r.min <= r.max);
    }
    CHECK((!r.min_exclusive || r.has_min));
    for (const double v : r.values) {
      CHECK((!r.has_min || v >= r.min));
      CHECK((!r.has_max || v <= r.max));
    }
  }
}

TEST_CASE("generated.field_ids match plan section 4.1") {
  const auto& elevation = field_info(FieldId::elevation);
  CHECK(elevation.dtype == DType::f32);
  CHECK(elevation.unit == "eu");
  CHECK(elevation.range.has_min);
  CHECK(elevation.range.min == 0.0);
  CHECK(elevation.range.max == 400.0);

  const auto& salinity = field_info(FieldId::salinity);
  CHECK(salinity.dtype == DType::u8);
  REQUIRE(salinity.range.values.size() == 4);
  CHECK(salinity.range.values[1] == 33.0);

  const auto& erodibility = field_info(FieldId::erodibility);
  CHECK(erodibility.range.min_exclusive);
  CHECK_FALSE(erodibility.range.has_max);

  CHECK(field_info(FieldId::flow_accum).range.note == ">= dx^2");
  CHECK(field_info(FieldId::flow_receiver).dtype == DType::i32);

  // Enumerated fields expose their labels and a 0..n-1 range.
  CHECK(static_cast<int>(wg::core::Water::river) == 3);
  CHECK(field_info(FieldId::water).range.max == 3.0);
  CHECK(static_cast<int>(wg::core::Aquifer::heavy) == 2);
}
