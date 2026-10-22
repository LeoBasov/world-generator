#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>

#include "wg/generated/field_ids.hpp"

namespace wg::pipeline {

/// Vector layers of plan section 4.2, in the order of core::VectorLayers. Stages name them in
/// their inputs and outputs next to the raster fields (plan sections 4.3 and 4.4).
enum class LayerId : std::uint8_t {
  peaks,
  volcanoes,
  river_graph,
  lakes,
  regions,
  plates,
  geo_columns,
  minerals,
};

/// Number of vector layers.
inline constexpr std::size_t kLayerCount = 8;

/// Name of a layer, e.g. "river_graph".
std::string_view to_string(LayerId layer) noexcept;

/// Layer with the given name, or nullopt.
std::optional<LayerId> layer_id_from_name(std::string_view name) noexcept;

/// A raster field or a vector layer: one element of a FieldSet. Converts implicitly from both,
/// so stages can write {FieldId::elevation, LayerId::peaks}.
struct FieldOrLayer {
  // NOLINTNEXTLINE(google-explicit-constructor)
  constexpr FieldOrLayer(core::FieldId field) noexcept : index(static_cast<std::uint16_t>(field)) {}
  // NOLINTNEXTLINE(google-explicit-constructor)
  constexpr FieldOrLayer(LayerId layer) noexcept
      : index(static_cast<std::uint16_t>(layer)), is_layer(true) {}

  std::uint16_t index = 0;  ///< FieldId or LayerId value
  bool is_layer = false;

  friend constexpr bool operator==(FieldOrLayer, FieldOrLayer) = default;
};

/// Name of a field (from the registry) or of a layer.
std::string_view to_string(FieldOrLayer item) noexcept;

/// Set of raster fields and vector layers: the inputs or outputs of a stage (plan section 2,
/// R2). Iteration visits the fields in FieldId order, then the layers in LayerId order.
class FieldSet {
 public:
  constexpr FieldSet() noexcept = default;
  constexpr FieldSet(std::initializer_list<FieldOrLayer> items) noexcept {
    for (const FieldOrLayer item : items) {
      add(item);
    }
  }

  constexpr FieldSet& add(FieldOrLayer item) noexcept {
    if (item.is_layer) {
      layers_ |= std::uint64_t{1} << item.index;
    } else {
      fields_[item.index / 64U] |= std::uint64_t{1} << (item.index % 64U);
    }
    return *this;
  }

  [[nodiscard]] constexpr bool contains(FieldOrLayer item) const noexcept {
    if (item.is_layer) {
      return ((layers_ >> item.index) & 1U) != 0;
    }
    return ((fields_[item.index / 64U] >> (item.index % 64U)) & 1U) != 0;
  }

  [[nodiscard]] constexpr bool empty() const noexcept { return size() == 0; }

  [[nodiscard]] constexpr std::size_t size() const noexcept {
    std::size_t n = static_cast<std::size_t>(std::popcount(layers_));
    for (const std::uint64_t w : fields_) {
      n += static_cast<std::size_t>(std::popcount(w));
    }
    return n;
  }

  /// Union.
  constexpr FieldSet& operator|=(const FieldSet& other) noexcept {
    for (std::size_t i = 0; i < kFieldWords; ++i) {
      fields_[i] |= other.fields_[i];
    }
    layers_ |= other.layers_;
    return *this;
  }
  friend constexpr FieldSet operator|(FieldSet a, const FieldSet& b) noexcept { return a |= b; }

  /// Elements of this set that are not in other.
  [[nodiscard]] constexpr FieldSet minus(const FieldSet& other) const noexcept {
    FieldSet r = *this;
    for (std::size_t i = 0; i < kFieldWords; ++i) {
      r.fields_[i] &= ~other.fields_[i];
    }
    r.layers_ &= ~other.layers_;
    return r;
  }

  [[nodiscard]] constexpr bool is_subset_of(const FieldSet& other) const noexcept {
    return minus(other).empty();
  }

  /// Calls f(FieldOrLayer) for every element: fields in id order, then layers.
  template <class F>
  constexpr void for_each(F&& f) const {
    for (std::size_t i = 0; i < core::kFieldCount; ++i) {
      if (((fields_[i / 64U] >> (i % 64U)) & 1U) != 0) {
        f(FieldOrLayer(static_cast<core::FieldId>(i)));
      }
    }
    for (std::size_t i = 0; i < kLayerCount; ++i) {
      if (((layers_ >> i) & 1U) != 0) {
        f(FieldOrLayer(static_cast<LayerId>(i)));
      }
    }
  }

  friend constexpr bool operator==(const FieldSet&, const FieldSet&) = default;

 private:
  static constexpr std::size_t kFieldWords = (core::kFieldCount + 63) / 64;
  static_assert(kLayerCount <= 64);

  std::array<std::uint64_t, kFieldWords> fields_{};
  std::uint64_t layers_ = 0;
};

/// Names of the elements separated by ", ", e.g. "elevation, peaks"; empty for an empty set.
std::string to_string(const FieldSet& set) noexcept;

}  // namespace wg::pipeline
