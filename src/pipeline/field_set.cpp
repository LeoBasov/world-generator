#include "wg/pipeline/field_set.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace wg::pipeline {

namespace {

constexpr std::array<std::string_view, kLayerCount> kLayerNames = {
    "peaks", "volcanoes", "river_graph", "lakes", "regions", "plates", "geo_columns", "minerals",
};

}  // namespace

std::string_view to_string(LayerId layer) noexcept {
  const auto i = static_cast<std::size_t>(layer);
  return i < kLayerCount ? kLayerNames[i] : std::string_view("unknown");
}

std::optional<LayerId> layer_id_from_name(std::string_view name) noexcept {
  for (std::size_t i = 0; i < kLayerCount; ++i) {
    if (kLayerNames[i] == name) {
      return static_cast<LayerId>(i);
    }
  }
  return std::nullopt;
}

std::string_view to_string(FieldOrLayer item) noexcept {
  if (item.is_layer) {
    return to_string(static_cast<LayerId>(item.index));
  }
  if (item.index >= core::kFieldCount) {
    return "unknown";
  }
  return core::field_info(static_cast<core::FieldId>(item.index)).name;
}

std::string to_string(const FieldSet& set) noexcept {
  std::string text;
  set.for_each([&](FieldOrLayer item) {
    if (!text.empty()) {
      text += ", ";
    }
    text += to_string(item);
  });
  return text;
}

}  // namespace wg::pipeline
