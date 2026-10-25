#include "wg/stages/classic.hpp"

#include <doctest/doctest.h>

#include <string_view>
#include <vector>

using wg::core::StatusCode;
using wg::pipeline::StageRegistry;

TEST_CASE("stages.add_classic_stages registers the classic stages under their ids") {
  StageRegistry registry;
  REQUIRE(wg::stages::add_classic_stages(registry).ok());
  CHECK(registry.ids() == std::vector<std::string_view>{"c01_allocate", "c02_seed", "c03_fit",
                                                        "c04_vegetation", "c05_shape", "c06_rivers",
                                                        "c07_smooth", "c08_climate", "c09_biomes",
                                                        "c10_subsurface"});
  for (const std::string_view id : registry.ids()) {
    CAPTURE(id);
    auto stage = registry.create(id);
    REQUIRE(stage);
    CHECK((*stage)->id() == id);
  }
}

TEST_CASE("stages.add_classic_stages refuses to register twice") {
  StageRegistry registry;
  REQUIRE(wg::stages::add_classic_stages(registry).ok());
  CHECK(wg::stages::add_classic_stages(registry).code == StatusCode::already_exists);
}
