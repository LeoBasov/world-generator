#include "wg/params/params.hpp"

#include <doctest/doctest.h>

#include <cstdint>

namespace {

using wg::params::find_param;
using wg::params::GenParams;
using wg::params::get_param;
using wg::params::param_label;
using wg::params::ParamDescriptor;
using wg::params::set_param;

const ParamDescriptor& param(const char* path) {
  const ParamDescriptor* d = find_param(path);
  REQUIRE(d != nullptr);
  return *d;
}

}  // namespace

TEST_CASE("params.params get and set reach the struct member of every kind") {
  GenParams p;
  set_param(p, param("$.grid.dx_m"), 0, 250.5);
  CHECK(p.grid.dx_m == 250.5);
  CHECK(get_param(p, param("$.grid.dx_m")) == 250.5);

  set_param(p, param("$.grid.size"), 0, 65.0);
  CHECK(p.grid.size == 65);
  set_param(p, param("$.grid.size"), 0, 33.9);  // truncated toward zero
  CHECK(p.grid.size == 33);

  set_param(p, param("$.erosion.PERIODICALLY_ERODE_EXTREMES"), 0, 0.0);
  CHECK(!p.erosion.periodically_erode_extremes);
  CHECK(get_param(p, param("$.erosion.PERIODICALLY_ERODE_EXTREMES")) == 0.0);

  set_param(p, param("$.poles"), 0, 3.0);
  CHECK(p.poles == wg::params::Poles::both);
  CHECK(get_param(p, param("$.poles")) == 3.0);
}

TEST_CASE("params.params array elements are addressed by index") {
  GenParams p;
  const ParamDescriptor& w = param("$.fields.drainage.weights");
  REQUIRE(w.count == 5);
  set_param(p, w, 4, 9.0);
  CHECK(p.fields.drainage.weights[4] == 9.0);
  CHECK(p.fields.drainage.weights[3] == 1.0);
  CHECK(get_param(p, w, 4) == 9.0);

  const ParamDescriptor& rivers = param("$.checks.RIVER_MINS");
  set_param(p, rivers, 1, 42.0);
  CHECK(p.checks.river_mins[1] == 42);
  CHECK(p.checks.river_mins[0] == 0);
}

TEST_CASE("params.params enumeration labels") {
  GenParams p;
  CHECK(param_label(p, param("$.poles")) == "north");
  p.poles = wg::params::Poles::none;
  CHECK(param_label(p, param("$.poles")) == "none");
  CHECK(param_label(p, param("$.grid.size")).empty());
  set_param(p, param("$.poles"), 0, 7.0);  // outside the labels
  CHECK(param_label(p, param("$.poles")).empty());
}

TEST_CASE("params.params documents compare by value") {
  wg::params::ParamsDocument a;
  wg::params::ParamsDocument b;
  CHECK(a == b);
  b.seed = std::uint64_t{7};
  CHECK(a != b);
  a.seed = std::uint64_t{7};
  a.params.grid.hu_m = 2.0;
  CHECK(a != b);
}
