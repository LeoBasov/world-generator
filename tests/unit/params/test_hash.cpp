#include "wg/params/hash.hpp"

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "io/json.hpp"
#include "support/source_files.hpp"
#include "wg/params/load.hpp"

namespace {

using wg::params::GenParams;
using wg::params::kParamDescriptors;
using wg::params::ParamDescriptor;
using wg::params::ParamKind;
using wg::params::params_hash;

GenParams load(std::string_view json) {
  const auto doc = wg::params::load_params(json);
  REQUIRE(doc.has_value());
  return doc->params;
}

}  // namespace

TEST_CASE("params.hash JSON round trip preserves the hash") {
  std::vector<std::string> texts = {R"({"schema_version": 1})"};
  for (const std::filesystem::path& path : wg::test::json_files("data/presets")) {
    const auto text = wg::io::read_text_file(path);
    REQUIRE(text.has_value());
    texts.push_back(*text);
  }
  for (const std::string& text : texts) {
    const auto doc = wg::params::load_params(text);
    REQUIRE(doc.has_value());
    const auto json = wg::params::to_json(*doc);
    REQUIRE(json.has_value());
    const auto back = wg::params::load_params(*json);
    REQUIRE(back.has_value());
    CHECK(params_hash(back->params) == params_hash(doc->params));
  }
}

TEST_CASE("params.hash depends on values, not on how the file is written") {
  const std::uint64_t h = params_hash(GenParams{});
  CHECK(params_hash(load(R"({"schema_version": 1})")) == h);
  // An explicit default, other key order and other whitespace.
  CHECK(params_hash(load(R"({"grid": {"hu_m": 10.0, "size": 129}, "schema_version": 1})")) == h);
  CHECK(params_hash(load("{\n\"schema_version\":1,\n\"poles\":\"north\"}")) == h);
  // Seed and profile are run defaults, not parameters.
  CHECK(params_hash(load(R"({"schema_version": 1, "seed": 5, "profile": "x"})")) == h);
}

TEST_CASE("params.hash changes with every single parameter element") {
  const GenParams base{};
  const std::uint64_t h = params_hash(base);
  std::set<std::uint64_t> seen = {h};
  for (const ParamDescriptor& d : kParamDescriptors) {
    const std::size_t n = d.count == 0 ? 1 : d.count;
    for (std::size_t i = 0; i < n; ++i) {
      CAPTURE(d.path);
      CAPTURE(i);
      GenParams p = base;
      const double v = wg::params::get_param(p, d, i);
      double changed = v + 1.0;
      if (d.kind == ParamKind::boolean) {
        changed = v == 0.0 ? 1.0 : 0.0;
      } else if (d.kind == ParamKind::enumeration) {
        changed = v == 0.0 ? 1.0 : 0.0;
      }
      wg::params::set_param(p, d, i, changed);
      const std::uint64_t hp = params_hash(p);
      CHECK(hp != h);
      CHECK(seen.insert(hp).second);  // and differently from every other change
    }
  }
}

TEST_CASE("params.hash treats -0.0 as +0.0") {
  GenParams a;
  GenParams b;
  a.fields.rainfall.min = 0.0;
  b.fields.rainfall.min = -0.0;
  CHECK(params_hash(a) == params_hash(b));
}
