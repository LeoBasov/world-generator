#include "wg/generated/gen_params.hpp"

#include <doctest/doctest.h>

#include <cstddef>
#include <set>
#include <string>
#include <string_view>

#include "wg/params/params.hpp"

namespace {

using wg::params::find_param;
using wg::params::GenParams;
using wg::params::kParamCount;
using wg::params::kParamDescriptors;
using wg::params::ParamDescriptor;
using wg::params::ParamKind;

// The descriptor table is usable at compile time.
static_assert(kParamCount == kParamDescriptors.size());
static_assert(wg::params::kParamsSchemaVersion == 1);
static_assert(find_param("$.grid.size") != nullptr);
static_assert(find_param("$.grid.size")->kind == ParamKind::integer);
static_assert(find_param("$.no.such") == nullptr);

std::size_t elements(const ParamDescriptor& d) { return d.count == 0 ? 1 : d.count; }

}  // namespace

TEST_CASE("generated.gen_params descriptor paths are unique JSON paths") {
  std::set<std::string_view> paths;
  for (const ParamDescriptor& d : kParamDescriptors) {
    CAPTURE(d.path);
    CHECK(d.path.substr(0, 2) == "$.");
    CHECK(paths.insert(d.path).second);
    CHECK(!d.description.empty());
  }
}

TEST_CASE("generated.gen_params defaults lie in range and match the member initialisers") {
  const GenParams defaults{};
  for (const ParamDescriptor& d : kParamDescriptors) {
    CAPTURE(d.path);
    REQUIRE(d.defaults.size() == elements(d));
    CHECK(d.min <= d.max);
    for (std::size_t i = 0; i < elements(d); ++i) {
      CAPTURE(i);
      CHECK(d.defaults[i] >= d.min);
      CHECK(d.defaults[i] <= d.max);
      CHECK(wg::params::get_param(defaults, d, i) == d.defaults[i]);
    }
  }
}

TEST_CASE("generated.gen_params every descriptor addresses its own member") {
  GenParams p;
  std::set<const void*> members;
  for (const ParamDescriptor& d : kParamDescriptors) {
    CAPTURE(d.path);
    const void* m = d.member(p);
    CHECK(m != nullptr);
    CHECK(members.insert(m).second);
    CHECK(m >= static_cast<const void*>(&p));
    CHECK(m < static_cast<const void*>(&p + 1));
  }
}

TEST_CASE("generated.gen_params enumerations carry their labels") {
  const ParamDescriptor* poles = find_param("$.poles");
  REQUIRE(poles != nullptr);
  CHECK(poles->kind == ParamKind::enumeration);
  REQUIRE(poles->labels.size() == 4);
  CHECK(poles->labels[0] == "none");
  CHECK(poles->labels[3] == "both");
  CHECK(poles->max == 3.0);
  CHECK(GenParams{}.poles == wg::params::Poles::north);
}

TEST_CASE("generated.gen_params instance defaults override the shared struct") {
  const GenParams p{};
  CHECK(p.fields.elevation.max == 400.0);
  CHECK(p.fields.rainfall.max == 100.0);
  CHECK(p.fields.temperature.min == 25.0);
  CHECK(wg::params::FieldGenParams{}.max == 100.0);
  CHECK(find_param("$.fields.elevation.weights")->df_name == "ELEVATION_FREQUENCY");
  CHECK(find_param("$.checks.REGION_COUNTS.HILLS")->df_name == "REGION_COUNTS:HILLS");
}
