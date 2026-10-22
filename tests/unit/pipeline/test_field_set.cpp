#include "wg/pipeline/field_set.hpp"

#include <doctest/doctest.h>

#include <string>
#include <vector>

using wg::core::FieldId;
using wg::pipeline::FieldOrLayer;
using wg::pipeline::FieldSet;
using wg::pipeline::LayerId;

TEST_CASE("pipeline.field_set holds fields and layers") {
  constexpr FieldSet s{FieldId::elevation, LayerId::peaks, FieldId::latitude};
  static_assert(s.contains(FieldId::elevation));
  static_assert(s.contains(LayerId::peaks));
  static_assert(!s.contains(FieldId::rainfall));
  // A field and a layer with the same index are different elements.
  static_assert(!s.contains(LayerId::volcanoes));  // index 1, as FieldId::elevation
  static_assert(s.size() == 3);
  CHECK_FALSE(s.empty());
  CHECK(FieldSet{}.empty());
  CHECK(FieldSet{}.size() == 0);
  CHECK(FieldSet{}.add(FieldId::aquifer).contains(FieldId::aquifer));
}

TEST_CASE("pipeline.field_set union, difference and subset") {
  const FieldSet a{FieldId::elevation, FieldId::rainfall, LayerId::lakes};
  const FieldSet b{FieldId::rainfall, LayerId::regions};
  CHECK((a | b) ==
        FieldSet{FieldId::elevation, FieldId::rainfall, LayerId::lakes, LayerId::regions});
  CHECK(a.minus(b) == FieldSet{FieldId::elevation, LayerId::lakes});
  CHECK(b.minus(a) == FieldSet{LayerId::regions});
  CHECK(a.minus(a).empty());
  CHECK(FieldSet{FieldId::rainfall}.is_subset_of(a));
  CHECK_FALSE(b.is_subset_of(a));
  CHECK(FieldSet{}.is_subset_of(FieldSet{}));
  FieldSet c = a;
  c |= b;
  CHECK(c == (a | b));
}

TEST_CASE("pipeline.field_set iterates fields in id order, then layers") {
  const FieldSet s{LayerId::minerals, FieldId::aquifer, LayerId::peaks, FieldId::latitude};
  std::vector<std::string> names;
  s.for_each([&](FieldOrLayer item) { names.emplace_back(wg::pipeline::to_string(item)); });
  CHECK(names == std::vector<std::string>{"latitude", "aquifer", "peaks", "minerals"});
  CHECK(wg::pipeline::to_string(s) == "latitude, aquifer, peaks, minerals");
  CHECK(wg::pipeline::to_string(FieldSet{}).empty());
}

TEST_CASE("pipeline.field_set layer names round trip") {
  for (std::size_t i = 0; i < wg::pipeline::kLayerCount; ++i) {
    const auto layer = static_cast<LayerId>(i);
    CHECK(wg::pipeline::layer_id_from_name(wg::pipeline::to_string(layer)) == layer);
  }
  CHECK(wg::pipeline::to_string(LayerId::river_graph) == "river_graph");
  CHECK_FALSE(wg::pipeline::layer_id_from_name("rivers"));
  CHECK(wg::pipeline::to_string(static_cast<LayerId>(200)) == "unknown");
  CHECK(wg::pipeline::to_string(FieldOrLayer(FieldId::biome)) == "biome");
}
