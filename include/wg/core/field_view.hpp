#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "wg/core/grid.hpp"

namespace wg::core {

/// Non-owning, row-major view of one raster field. Kernels take data as a span; stages use
/// the (x, y) accessor. A view stays valid until the field is released or its WorldState dies.
template <class T>
struct FieldView {
  std::span<T> data;
  Grid grid;

  [[nodiscard]] constexpr T& operator()(std::int32_t x, std::int32_t y) const noexcept {
    return data[static_cast<std::size_t>(grid.idx(x, y))];
  }
  [[nodiscard]] constexpr T& operator[](std::int64_t i) const noexcept {
    return data[static_cast<std::size_t>(i)];
  }
  [[nodiscard]] constexpr std::int64_t size() const noexcept {
    return static_cast<std::int64_t>(data.size());
  }
  /// Read-only view of the same data.
  [[nodiscard]] constexpr FieldView<const T> as_const() const noexcept {
    return {std::span<const T>(data), grid};
  }
};

}  // namespace wg::core
