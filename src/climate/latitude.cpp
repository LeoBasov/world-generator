#include "wg/climate/latitude.hpp"

#include <cstddef>
#include <cstdint>

#include "numerics/common.hpp"

namespace wg::climate {

namespace detail = num::detail;

core::Status fill_latitude(params::Poles poles, const core::Grid& grid, std::span<float> phi,
                           core::Executor& exec, const core::CancelFlag* cancel) noexcept {
  core::Status status = detail::first_error(
      {core::validate(grid),
       detail::check_field(phi, grid, "fill_latitude: phi must have one value per cell")});
  if (!status.ok()) {
    return status;
  }
  const std::int32_t n = grid.n;
  const auto rows = [&](const core::ChunkRange& r) {
    for (std::int64_t y = r.begin; y < r.end; ++y) {
      const auto value =
          static_cast<float>(latitude_of_row(poles, n, static_cast<std::int32_t>(y)));
      const auto first = static_cast<std::size_t>(y * n);
      for (std::size_t i = first; i < first + static_cast<std::size_t>(n); ++i) {
        phi[i] = value;
      }
    }
  };
  return core::parallel_for(exec, 0, n, detail::kRowGrain, rows, cancel);
}

}  // namespace wg::climate
