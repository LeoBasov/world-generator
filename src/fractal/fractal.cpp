#include "wg/fractal/fractal.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <string_view>
#include <utility>

#include "numerics/common.hpp"
#include "wg/core/arena.hpp"
#include "wg/core/detmath.hpp"
#include "wg/core/distributions.hpp"

namespace wg::fractal {

namespace {

namespace detail = num::detail;

// Variance scale of R3.4: sigma_0 = noise_scale * (V / kVarianceScale) * (max - min).
constexpr double kVarianceScale = 3200.0;

core::Status invalid(std::string_view message) {
  return core::make_status(core::StatusCode::invalid_argument, message);
}

bool valid_controls(ControlGrid c) {
  return c.cells > 0 && c.spacing > 0 && std::has_single_bit(static_cast<std::uint32_t>(c.cells)) &&
         std::has_single_bit(static_cast<std::uint32_t>(c.spacing));
}

core::Status check_range(const params::FieldGenParams& p, std::string_view message) {
  if (!std::isfinite(p.min) || !std::isfinite(p.max) || p.min > p.max) {
    return invalid(message);
  }
  return {};
}

core::Status check_noise(const params::FieldGenParams& p) {
  if (!std::isfinite(p.x_variance) || !std::isfinite(p.y_variance) || p.x_variance < 0.0 ||
      p.y_variance < 0.0) {
    return invalid("diamond_square: variances must be finite and not negative");
  }
  if (!(p.hurst > 0.0 && p.hurst < 1.0)) {
    return invalid("diamond_square: hurst must lie in (0, 1)");
  }
  if (!std::isfinite(p.noise_scale) || p.noise_scale < 0.0) {
    return invalid("diamond_square: noise_scale must be finite and not negative");
  }
  return {};
}

// Displacement amplitudes at level 0 for points between east-west neighbours (x), between
// north-south neighbours (y) and at cell centres.
struct Sigma0 {
  double x = 0.0;
  double y = 0.0;
  double centre = 0.0;
};

Sigma0 sigma0(const params::FieldGenParams& p) {
  const double range = p.max - p.min;
  Sigma0 s;
  s.x = p.noise_scale * (p.x_variance / kVarianceScale) * range;
  s.y = p.noise_scale * (p.y_variance / kVarianceScale) * range;
  s.centre = 0.5 * (s.x + s.y);
  return s;
}

// Index of control point (cx, cy) in a grid of m control cells per axis.
std::size_t control_index(std::int32_t cy, std::int32_t cx, std::int32_t m) {
  return static_cast<std::size_t>((static_cast<std::int64_t>(cy) * (m + 1)) + cx);
}

// Mean of the values at cells a and b.
double pair_mean(std::span<const float> z, std::int64_t a, std::int64_t b) {
  return (static_cast<double>(z[static_cast<std::size_t>(a)]) +
          static_cast<double>(z[static_cast<std::size_t>(b)])) /
         2.0;
}

// Mean of the four points at distance h from the centre (x, y), in kDir4 order. A centre lies
// inside the grid, so all four exist.
double centre_mean(std::span<const float> z, const core::Grid& grid, std::int32_t x, std::int32_t y,
                   std::int32_t h) {
  double sum = 0.0;
  for (const core::Dir8 d : core::kDir4) {
    sum += static_cast<double>(
        z[static_cast<std::size_t>(grid.idx(x + (core::dx(d) * h), y + (core::dy(d) * h)))]);
  }
  return sum / 4.0;
}

}  // namespace

core::Expected<ControlGrid> control_grid(const core::Grid& grid, std::int32_t mesh) noexcept {
  core::Status status = core::validate(grid);
  if (!status.ok()) {
    return core::make_error(std::move(status));
  }
  if (mesh < 1 || mesh > kMaxMesh) {
    return core::make_error(invalid("control_grid: mesh must lie in [1, 6]"));
  }
  const std::int32_t cells = std::min(std::int32_t{1} << (mesh - 1), grid.n - 1);
  return ControlGrid{.cells = cells, .spacing = (grid.n - 1) / cells};
}

core::Status pin_edges(ControlGrid controls, std::uint8_t edges, std::int8_t bin,
                       std::span<std::int8_t> pinned_bins) noexcept {
  if (!valid_controls(controls)) {
    return invalid("pin_edges: invalid control grid");
  }
  if (static_cast<std::int64_t>(pinned_bins.size()) != control_count(controls)) {
    return invalid("pin_edges: pinned_bins must have one entry per control point");
  }
  if (bin < kFreeControl || bin >= kBinCount) {
    return invalid("pin_edges: bin must lie in [-1, 4]");
  }
  const std::int32_t m = controls.cells;
  for (std::int32_t cy = 0; cy <= m; ++cy) {
    for (std::int32_t cx = 0; cx <= m; ++cx) {
      const bool on_edge =
          ((edges & kEdgeNorth) != 0 && cy == 0) || ((edges & kEdgeSouth) != 0 && cy == m) ||
          ((edges & kEdgeWest) != 0 && cx == 0) || ((edges & kEdgeEast) != 0 && cx == m);
      if (on_edge) {
        pinned_bins[control_index(cy, cx, m)] = bin;
      }
    }
  }
  return {};
}

core::Status sample_controls(const params::FieldGenParams& params, ControlGrid controls,
                             core::RngKey key, std::span<const std::int8_t> pinned_bins,
                             std::span<float> values) noexcept {
  if (!valid_controls(controls)) {
    return invalid("sample_controls: invalid control grid");
  }
  core::Status status =
      check_range(params, "sample_controls: min and max must be finite with min <= max");
  if (!status.ok()) {
    return status;
  }
  const std::int64_t count = control_count(controls);
  if (static_cast<std::int64_t>(values.size()) != count) {
    return invalid("sample_controls: values must have one entry per control point");
  }
  if (!pinned_bins.empty() && static_cast<std::int64_t>(pinned_bins.size()) != count) {
    return invalid("sample_controls: pinned_bins must be empty or one entry per control point");
  }
  for (const std::int8_t pin : pinned_bins) {
    if (pin < kFreeControl || pin >= kBinCount) {
      return invalid("sample_controls: a pinned bin must lie in [-1, 4]");
    }
  }
  // With mesh = 1 the weights are unused (R3.2): every bin is equally likely.
  constexpr std::array<double, kBinCount> kEqual = {1.0, 1.0, 1.0, 1.0, 1.0};
  const std::span<const double> weights =
      params.mesh == 1 ? std::span<const double>(kEqual) : std::span<const double>(params.weights);
  core::Expected<core::WeightedBins> bins = core::WeightedBins::create(weights);
  if (!bins) {
    return std::move(bins.error());
  }
  const core::RngKey control_key = key.derive("control");
  const double delta = (params.max - params.min) / static_cast<double>(kBinCount);
  for (std::int64_t i = 0; i < count; ++i) {
    const auto k = static_cast<std::size_t>(i);
    const auto drawn = static_cast<std::int32_t>(bins->sample(control_key, k, 0));
    const std::int32_t bin = pinned_bins.empty() || pinned_bins[k] == kFreeControl
                                 ? drawn
                                 : static_cast<std::int32_t>(pinned_bins[k]);
    const double u = core::uniform01(control_key, k, 1);
    values[k] = static_cast<float>(params.min + ((static_cast<double>(bin) + u) * delta));
  }
  return {};
}

core::Status diamond_square(std::span<const float> control_values, ControlGrid controls,
                            const core::Grid& grid, const params::FieldGenParams& params,
                            core::RngKey key, std::span<float> out, core::Executor& exec,
                            const core::CancelFlag* cancel) noexcept {
  core::Status status = detail::first_error(
      {core::validate(grid),
       check_range(params,
                   "diamond_square: min and max must be finite "
                   "with min <= max"),
       check_noise(params),
       detail::check_field(out, grid, "diamond_square: out must have one value per cell")});
  if (!status.ok()) {
    return status;
  }
  if (!valid_controls(controls) ||
      static_cast<std::int64_t>(controls.cells) * controls.spacing != grid.n - 1) {
    return invalid("diamond_square: the control grid does not match the grid");
  }
  if (static_cast<std::int64_t>(control_values.size()) != control_count(controls)) {
    return invalid("diamond_square: control_values must have one entry per control point");
  }

  const std::int32_t m = controls.cells;
  const std::int32_t s0 = controls.spacing;
  for (std::int32_t cy = 0; cy <= m; ++cy) {
    for (std::int32_t cx = 0; cx <= m; ++cx) {
      out[static_cast<std::size_t>(grid.idx(cx * s0, cy * s0))] =
          control_values[control_index(cy, cx, m)];
    }
  }

  const core::RngKey noise_key = key.derive("displace");
  const Sigma0 base = sigma0(params);
  const std::int32_t n = grid.n;
  const std::span<const float> done(out);
  std::int32_t level = 0;
  for (std::int32_t s = s0; s >= 2; s /= 2, ++level) {
    const std::int32_t h = s / 2;
    // 2^(-k H) through det_exp, so the amplitudes are the same bits on every platform (R1).
    const double decay =
        core::det_exp(-static_cast<double>(level) * params.hurst * std::numbers::ln2);
    const double sigma_x = base.x * decay;
    const double sigma_y = base.y * decay;
    const double sigma_c = base.centre * decay;
    const auto displace = [&](std::int32_t x, std::int32_t y, double mean, double sigma) {
      const std::int64_t i = grid.idx(x, y);
      const double xi =
          sigma > 0.0 ? core::normal(noise_key, static_cast<std::uint64_t>(i), 0, 0.0, 1.0) : 0.0;
      out[static_cast<std::size_t>(i)] = static_cast<float>(mean + (sigma * xi));
    };

    // Edge step: rows y = j h. On even rows the new points lie between east-west corners and
    // take their mean, on odd rows between north-south corners. Only the two collinear parents
    // count, so a point's displacement acts along its own axis (ADR 0009).
    const auto edges = [&](const core::ChunkRange& range) {
      for (std::int64_t j = range.begin; j < range.end; ++j) {
        const auto y = static_cast<std::int32_t>(j * h);
        if ((j % 2) == 0) {
          for (std::int32_t x = h; x < n; x += s) {
            displace(x, y, pair_mean(done, grid.idx(x - h, y), grid.idx(x + h, y)), sigma_x);
          }
        } else {
          for (std::int32_t x = 0; x < n; x += s) {
            displace(x, y, pair_mean(done, grid.idx(x, y - h), grid.idx(x, y + h)), sigma_y);
          }
        }
      }
    };
    status = core::parallel_for(exec, 0, ((n - 1) / h) + 1, detail::kRowGrain, edges, cancel);
    if (!status.ok()) {
      return status;
    }

    // Centre step: the centres (x, y) = (h + c s, h + r s), from the four edge points around.
    const auto centres = [&](const core::ChunkRange& range) {
      for (std::int64_t r = range.begin; r < range.end; ++r) {
        const auto y = static_cast<std::int32_t>(h + (r * s));
        for (std::int32_t x = h; x < n; x += s) {
          displace(x, y, centre_mean(done, grid, x, y, h), sigma_c);
        }
      }
    };
    status = core::parallel_for(exec, 0, (n - 1) / s, detail::kRowGrain, centres, cancel);
    if (!status.ok()) {
      return status;
    }
  }
  return {};
}

core::Status clamp_field(std::span<float> z, double lo, double hi, core::Executor& exec) noexcept {
  if (!std::isfinite(lo) || !std::isfinite(hi) || lo > hi) {
    return invalid("clamp_field: bounds must be finite with lo <= hi");
  }
  const auto flo = static_cast<float>(lo);
  const auto fhi = static_cast<float>(hi);
  const auto body = [&](const core::ChunkRange& r) {
    for (std::int64_t i = r.begin; i < r.end; ++i) {
      float& v = z[static_cast<std::size_t>(i)];
      v = std::clamp(v, flo, fhi);
    }
  };
  return core::parallel_for(exec, 0, static_cast<std::int64_t>(z.size()), detail::kValueGrain,
                            body);
}

core::Status generate_unclamped_field(const core::Grid& grid, const params::FieldGenParams& params,
                                      core::RngKey key, std::span<const std::int8_t> pinned_bins,
                                      std::span<float> out, core::Executor& exec,
                                      const core::CancelFlag* cancel,
                                      core::MemoryResource& resource) noexcept {
  core::Expected<ControlGrid> controls = control_grid(grid, params.mesh);
  if (!controls) {
    return std::move(controls.error());
  }
  const auto count = static_cast<std::size_t>(control_count(*controls));
  core::Expected<core::Arena> arena =
      core::Arena::create(detail::arena_bytes<float>(count), resource);
  if (!arena) {
    return std::move(arena.error());
  }
  core::Expected<std::span<float>> values = arena->allocate<float>(count);
  if (!values) {
    return std::move(values.error());
  }
  core::Status status = sample_controls(params, *controls, key, pinned_bins, *values);
  if (status.ok()) {
    status = diamond_square(*values, *controls, grid, params, key, out, exec, cancel);
  }
  return status;
}

core::Status generate_field(const core::Grid& grid, const params::FieldGenParams& params,
                            core::RngKey key, std::span<const std::int8_t> pinned_bins,
                            std::span<float> out, core::Executor& exec,
                            const core::CancelFlag* cancel,
                            core::MemoryResource& resource) noexcept {
  core::Status status =
      generate_unclamped_field(grid, params, key, pinned_bins, out, exec, cancel, resource);
  if (status.ok()) {
    status = clamp_field(out, params.min, params.max, exec);
  }
  return status;
}

}  // namespace wg::fractal
