#pragma once

#include <cstdint>
#include <span>

#include "wg/core/cancel.hpp"
#include "wg/core/executor.hpp"
#include "wg/core/expected.hpp"
#include "wg/core/grid.hpp"
#include "wg/core/memory.hpp"
#include "wg/core/rng.hpp"
#include "wg/core/status.hpp"
#include "wg/generated/gen_params.hpp"

namespace wg::fractal {

// Fractal primary fields (R3.2-R3.4, A11): weighted control values on a coarse grid, filled by
// anisotropic diamond-square and clamped to [min, max]. The map is a bounded square (no wrap).
// The RNG layout and the level convention are frozen in docs/adr/0009-fractal-fields.md; changing
// them changes every world.

/// Control grid of mesh parameter m: cells control cells per axis, spacing grid cells apart.
struct ControlGrid {
  std::int32_t cells = 0;    ///< M = min(2^(m - 1), n - 1)
  std::int32_t spacing = 0;  ///< (n - 1) / M

  friend constexpr bool operator==(ControlGrid, ControlGrid) = default;
};

/// Largest mesh parameter (control grid of 32 cells per axis).
inline constexpr std::int32_t kMaxMesh = 6;

/// Number of weighted bins of [min, max].
inline constexpr std::int32_t kBinCount = 5;

/// Control grid of a field with mesh m on grid. m = 1 ("mesh ignored") gives the four corners.
/// A mesh finer than the grid is limited to n - 1 cells, so every grid point is then a control
/// point. Errors: invalid_argument for an invalid grid or m outside [1, kMaxMesh].
[[nodiscard]] core::Expected<ControlGrid> control_grid(const core::Grid& grid,
                                                       std::int32_t mesh) noexcept;

/// Number of control points, (M + 1)^2. Control point (cx, cy) has index cy * (M + 1) + cx and
/// lies at grid cell (cx * spacing, cy * spacing).
[[nodiscard]] constexpr std::int64_t control_count(ControlGrid c) noexcept {
  return static_cast<std::int64_t>(c.cells + 1) * (c.cells + 1);
}

/// Value of pinned_bins for a control point whose bin is drawn from the weights.
inline constexpr std::int8_t kFreeControl = -1;

/// Map edges for pin_edges, combined with |. Row 0 is the north edge, column 0 the west edge.
inline constexpr std::uint8_t kEdgeNorth = 1;
inline constexpr std::uint8_t kEdgeEast = 2;
inline constexpr std::uint8_t kEdgeSouth = 4;
inline constexpr std::uint8_t kEdgeWest = 8;
inline constexpr std::uint8_t kAllEdges = kEdgeNorth | kEdgeEast | kEdgeSouth | kEdgeWest;

/// Edge-pinning hook (R4.1: edge oceans are enforced by fixing the edge control points to the
/// lowest bin). Sets pinned_bins of every control point on the chosen edges to bin and leaves
/// the other entries as they are. Errors: invalid_argument for an invalid control grid, a span
/// without one entry per control point, or bin outside [kFreeControl, kBinCount - 1].
[[nodiscard]] core::Status pin_edges(ControlGrid controls, std::uint8_t edges, std::int8_t bin,
                                     std::span<std::int8_t> pinned_bins) noexcept;

/// Control values of R3.3: per control point a bin chosen with probability proportional to the
/// weights (or the pinned bin), then a value uniform inside it. With m = 1 the weights are unused
/// and every bin is equally likely. Draws are keyed by the control index, so the values do not
/// depend on the grid size. pinned_bins is empty (nothing pinned) or holds one entry per control
/// point, kFreeControl or a bin. Errors: invalid_argument for an invalid control grid, min > max,
/// a non-finite bound, bad weights, a bad pin or a span of the wrong size.
[[nodiscard]] core::Status sample_controls(const params::FieldGenParams& params,
                                           ControlGrid controls, core::RngKey key,
                                           std::span<const std::int8_t> pinned_bins,
                                           std::span<float> values) noexcept;

/// Anisotropic midpoint displacement (R3.4) from the control values, without clamping. Level
/// k = 0 is the first subdivision of the control grid. Each level first sets the edge midpoints,
/// as the mean of their two collinear parents, then the cell centres, as the mean of the four
/// edge midpoints around them; each new point gets sigma_k xi with xi ~ N(0, 1) and
/// sigma_k = noise_scale * (V / 3200) * (max - min) * 2^(-k H): V = x_variance for points
/// between east-west neighbours, y_variance between north-south neighbours, and their mean for
/// centres (ADR 0009). The result is identical for any executor.
/// Errors: invalid_argument for an invalid grid or control grid, bad parameters or spans of the
/// wrong size; those of core::parallel_for, cancelled included.
[[nodiscard]] core::Status diamond_square(std::span<const float> control_values,
                                          ControlGrid controls, const core::Grid& grid,
                                          const params::FieldGenParams& params, core::RngKey key,
                                          std::span<float> out, core::Executor& exec,
                                          const core::CancelFlag* cancel = nullptr) noexcept;

/// Clamps every value to [lo, hi]. Errors: invalid_argument for lo > hi or a non-finite bound;
/// those of core::parallel_for.
[[nodiscard]] core::Status clamp_field(std::span<float> z, double lo, double hi,
                                       core::Executor& exec) noexcept;

/// generate_field without the final clamp_field: control grid of params.mesh, sample_controls
/// and diamond_square. Stage C2 reads the raw elevation for its peak candidates before it clamps
/// (ADR 0010). The control values live in scratch memory from resource. Errors: those of the
/// steps, out_of_memory.
[[nodiscard]] core::Status generate_unclamped_field(
    const core::Grid& grid, const params::FieldGenParams& params, core::RngKey key,
    std::span<const std::int8_t> pinned_bins, std::span<float> out, core::Executor& exec,
    const core::CancelFlag* cancel = nullptr,
    core::MemoryResource& resource = core::default_memory_resource()) noexcept;

/// One primary field: generate_unclamped_field, then clamp_field to [min, max]. Errors: those
/// of the steps, out_of_memory.
[[nodiscard]] core::Status generate_field(
    const core::Grid& grid, const params::FieldGenParams& params, core::RngKey key,
    std::span<const std::int8_t> pinned_bins, std::span<float> out, core::Executor& exec,
    const core::CancelFlag* cancel = nullptr,
    core::MemoryResource& resource = core::default_memory_resource()) noexcept;

}  // namespace wg::fractal
