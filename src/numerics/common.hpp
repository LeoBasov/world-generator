#pragma once

// Helpers shared by the numerics kernels (not part of the public API).

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <string_view>

#include "wg/core/arena.hpp"
#include "wg/core/grid.hpp"
#include "wg/core/status.hpp"

namespace wg::num::detail {

/// Grid rows per chunk of a parallel loop. A constant (ADR 0003): changing it changes the
/// results of the reductions that use it.
inline constexpr std::int64_t kRowGrain = 16;

/// Values per chunk of a parallel loop over a flat span.
inline constexpr std::int64_t kValueGrain = 65536;

/// invalid_argument with message unless the span holds one value per grid cell.
template <class T>
core::Status check_field(std::span<T> values, const core::Grid& grid,
                         std::string_view message) noexcept {
  if (static_cast<std::int64_t>(values.size()) != grid.cells()) {
    return core::make_status(core::StatusCode::invalid_argument, message);
  }
  return {};
}

/// Like check_field, but an empty span (meaning "all cells" or "not wanted") is accepted.
template <class T>
core::Status check_optional_field(std::span<T> values, const core::Grid& grid,
                                  std::string_view message) noexcept {
  if (values.empty()) {
    return {};
  }
  return check_field(values, grid, message);
}

/// The first status that is not ok, or ok.
inline core::Status first_error(std::initializer_list<core::Status> statuses) noexcept {
  for (const core::Status& s : statuses) {
    if (!s.ok()) {
      return s;
    }
  }
  return {};
}

/// True if cell i belongs to the mask; an empty mask holds every cell.
inline bool in_mask(std::span<const std::uint8_t> mask, std::int64_t i) noexcept {
  return mask.empty() || mask[static_cast<std::size_t>(i)] != 0;
}

/// grid.neighbor(i, d) for every d of core::kDir8, in that order (core::kNoCell outside the
/// grid), with one division for the cell instead of two per direction. For hot loops.
inline std::array<std::int64_t, 8> neighbors8(const core::Grid& grid, std::int64_t i) noexcept {
  const std::int32_t x = grid.x_of(i);
  const std::int32_t y = grid.y_of(i);
  std::array<std::int64_t, 8> out{};
  for (std::size_t k = 0; k < core::kDir8.size(); ++k) {
    const std::int32_t nx = x + core::dx(core::kDir8[k]);
    const std::int32_t ny = y + core::dy(core::kDir8[k]);
    out[k] = grid.contains(nx, ny) ? grid.idx(nx, ny) : core::kNoCell;
  }
  return out;
}

/// Arena capacity for count values of T, with room for the alignment padding.
template <class T>
constexpr std::size_t arena_bytes(std::size_t count) noexcept {
  return (count * sizeof(T)) + core::Arena::kAlignment;
}

}  // namespace wg::num::detail
