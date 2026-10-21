#include "wg/numerics/laplacian.hpp"

#include <cstddef>
#include <cstdint>
#include <utility>

#include "common.hpp"
#include "wg/core/arena.hpp"
#include "wg/core/expected.hpp"

namespace wg::num {

namespace {

// Sum of (z_j - z_i) over the D4 neighbours j of i inside the grid and the mask.
double laplacian_at(std::span<const float> z, std::span<const std::uint8_t> mask,
                    const core::Grid& grid, std::int64_t i) {
  const double zi = z[static_cast<std::size_t>(i)];
  double sum = 0.0;
  for (const core::Dir8 d : core::kDir4) {
    const std::int64_t j = grid.neighbor(i, d);
    if (j != core::kNoCell && detail::in_mask(mask, j)) {
      sum += static_cast<double>(z[static_cast<std::size_t>(j)]) - zi;
    }
  }
  return sum;
}

core::Status check_args(std::span<const float> z, std::span<const std::uint8_t> mask,
                        const core::Grid& grid) {
  return detail::first_error(
      {core::validate(grid),
       detail::check_field(z, grid, "laplacian: z must have one value per cell"),
       detail::check_optional_field(mask, grid, "laplacian: mask must be empty or per cell")});
}

}  // namespace

core::Status laplacian(std::span<const float> z, std::span<const std::uint8_t> mask,
                       const core::Grid& grid, std::span<float> out,
                       core::Executor& exec) noexcept {
  core::Status status = detail::first_error(
      {check_args(z, mask, grid),
       detail::check_field(out, grid, "laplacian: out must have one value per cell")});
  if (!status.ok()) {
    return status;
  }
  const std::int64_t n = grid.n;
  const auto body = [&](const core::ChunkRange& r) {
    for (std::int64_t i = r.begin * n; i < r.end * n; ++i) {
      out[static_cast<std::size_t>(i)] =
          detail::in_mask(mask, i) ? static_cast<float>(laplacian_at(z, mask, grid, i)) : 0.0F;
    }
  };
  return core::parallel_for(exec, 0, n, detail::kRowGrain, body);
}

core::Status laplacian_smooth(std::span<float> z, std::span<const std::uint8_t> mask,
                              const core::Grid& grid, const SmoothParams& params,
                              core::Executor& exec, core::MemoryResource& resource) noexcept {
  core::Status status = check_args(z, mask, grid);
  if (!status.ok()) {
    return status;
  }
  if (!(params.alpha > 0.0 && params.alpha <= 0.25)) {
    return core::make_status(core::StatusCode::invalid_argument,
                             "laplacian_smooth: alpha must lie in (0, 0.25]");
  }
  if (params.iterations < 0) {
    return core::make_status(core::StatusCode::invalid_argument,
                             "laplacian_smooth: iterations must not be negative");
  }
  if (params.iterations == 0) {
    return {};
  }
  const auto cells = static_cast<std::size_t>(grid.cells());
  core::Expected<core::Arena> arena =
      core::Arena::create(detail::arena_bytes<float>(cells), resource);
  if (!arena) {
    return std::move(arena.error());
  }
  core::Expected<std::span<float>> next = arena->allocate<float>(cells);
  if (!next) {
    return std::move(next.error());
  }
  const std::span<const float> current(z);
  const std::span<float> scratch = *next;
  const double alpha = params.alpha;
  const std::int64_t n = grid.n;
  const auto step = [&](const core::ChunkRange& r) {
    for (std::int64_t i = r.begin * n; i < r.end * n; ++i) {
      const auto k = static_cast<std::size_t>(i);
      scratch[k] = detail::in_mask(mask, i)
                       ? static_cast<float>(static_cast<double>(current[k]) +
                                            (alpha * laplacian_at(current, mask, grid, i)))
                       : current[k];
    }
  };
  const auto copy_back = [&](const core::ChunkRange& r) {
    for (std::int64_t i = r.begin * n; i < r.end * n; ++i) {
      z[static_cast<std::size_t>(i)] = scratch[static_cast<std::size_t>(i)];
    }
  };
  for (std::int32_t it = 0; it < params.iterations; ++it) {
    status = core::parallel_for(exec, 0, n, detail::kRowGrain, step);
    if (status.ok()) {
      status = core::parallel_for(exec, 0, n, detail::kRowGrain, copy_back);
    }
    if (!status.ok()) {
      return status;
    }
  }
  return {};
}

}  // namespace wg::num
