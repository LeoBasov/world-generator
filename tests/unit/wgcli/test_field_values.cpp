#include "field_values.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <limits>
#include <vector>

#include "support/sample_world.hpp"
#include "wg/io/world_file.hpp"

using wg::core::FieldId;

TEST_CASE("wgcli.field_values selects and reads fields") {
  auto bytes = wg::io::write_world(wg::test::sample_world(), wg::test::sample_params());
  REQUIRE(bytes);
  auto reader = wg::io::WorldReader::open_buffer(*bytes);
  REQUIRE(reader);

  auto all = wg::cli::select_fields({}, *reader);
  REQUIRE(all);
  CHECK(all->size() == 6);
  CHECK(wg::cli::select_fields({"all"}, *reader)->size() == 6);
  auto two = wg::cli::select_fields({"biome", "water", "biome"}, *reader);
  REQUIRE(two);
  CHECK(*two == std::vector<FieldId>{FieldId::biome, FieldId::water});
  CHECK(wg::cli::select_fields({"nonsense"}, *reader).error().code ==
        wg::core::StatusCode::invalid_argument);
  CHECK(wg::cli::select_fields({"rainfall"}, *reader).error().code ==
        wg::core::StatusCode::not_found);

  for (const FieldId id : *all) {
    auto values = wg::cli::read_values(*reader, id);
    REQUIRE(values);
    CHECK(values->size() == 17U * 17U);
  }
  auto plate = wg::cli::read_values(*reader, FieldId::plate_id);
  REQUIRE(plate);
  CHECK((*plate)[0] == -100.0);
  auto biome = wg::cli::read_values(*reader, FieldId::biome);
  CHECK((*biome)[1] == 211.0);
  CHECK(wg::cli::read_values(*reader, FieldId::rainfall).error().code ==
        wg::core::StatusCode::not_found);
}

TEST_CASE("wgcli.field_values computes statistics of the finite values") {
  const double inf = std::numeric_limits<double>::infinity();
  const auto s = wg::cli::field_stats({2.0, -1.0, std::nan(""), 5.0, inf});
  CHECK(s.min == -1.0);
  CHECK(s.max == 5.0);
  CHECK(s.mean == 2.0);
  CHECK(s.finite == 3);
  CHECK(s.non_finite == 2);
  const auto empty = wg::cli::field_stats({});
  CHECK(empty.finite == 0);
  CHECK(empty.mean == 0.0);
}
