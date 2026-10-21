#include "wg/numerics/spectrum.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <utility>

#include "common.hpp"
#include "wg/core/arena.hpp"
#include "wg/numerics/fft.hpp"

namespace wg::num {

namespace {

constexpr double kPi = std::numbers::pi;

}  // namespace

core::Expected<RadialSpectrum> radial_power_spectrum(std::span<const float> z,
                                                     const core::Grid& grid,
                                                     const SpectrumParams& params,
                                                     core::MemoryResource& resource) noexcept {
  core::Status status = detail::first_error(
      {core::validate(grid),
       detail::check_field(z, grid, "radial_power_spectrum: z must have one value per cell")});
  if (!status.ok()) {
    return core::make_error(std::move(status));
  }
  const std::int32_t m = grid.n - 1;
  const auto side = static_cast<std::size_t>(m);
  core::Expected<core::Arena> arena = core::Arena::create(
      detail::arena_bytes<Complex>(side * side) + detail::arena_bytes<double>(side), resource);
  if (!arena) {
    return core::make_error(std::move(arena.error()));
  }
  core::Expected<std::span<Complex>> data = arena->allocate<Complex>(side * side);
  core::Expected<std::span<double>> window = arena->allocate<double>(side);
  if (!data || !window) {
    return core::make_error(core::StatusCode::out_of_memory, "radial_power_spectrum: no memory");
  }
  const std::span<Complex> f = *data;
  const std::span<double> win = *window;

  double sum = 0.0;
  for (std::int32_t y = 0; y < m; ++y) {
    for (std::int32_t x = 0; x < m; ++x) {
      const double v = z[static_cast<std::size_t>(grid.idx(x, y))];
      if (!std::isfinite(v)) {
        return core::make_error(core::StatusCode::invalid_argument,
                                "radial_power_spectrum: non-finite value");
      }
      sum += v;
    }
  }
  const double mean = sum / (static_cast<double>(m) * static_cast<double>(m));
  double w2 = 0.0;
  for (std::size_t x = 0; x < side; ++x) {
    const double phase = 2.0 * kPi * static_cast<double>(x) / static_cast<double>(side);
    const double w = params.hann ? 0.5 * (1.0 - std::cos(phase)) : 1.0;
    win[x] = w;
    w2 += w * w;
  }
  const double norm = w2 * w2;  // sum of the squared 2-D weights w(x) w(y)
  for (std::int32_t y = 0; y < m; ++y) {
    for (std::int32_t x = 0; x < m; ++x) {
      const double v = z[static_cast<std::size_t>(grid.idx(x, y))];
      const double w = win[static_cast<std::size_t>(x)] * win[static_cast<std::size_t>(y)];
      f[(static_cast<std::size_t>(y) * side) + static_cast<std::size_t>(x)] =
          Complex{(v - mean) * w, 0.0};
    }
  }
  status = fft_2d(f, m, m, false, resource);
  if (!status.ok()) {
    return core::make_error(std::move(status));
  }

  const std::int32_t bins = m / 2;
  RadialSpectrum result;
  try {
    result.freq.resize(static_cast<std::size_t>(bins));
    result.power.assign(static_cast<std::size_t>(bins), 0.0);
    result.modes.assign(static_cast<std::size_t>(bins), 0);
  } catch (...) {
    return core::make_error(core::StatusCode::out_of_memory, "radial_power_spectrum: no memory");
  }
  for (std::int32_t ky = 0; ky < m; ++ky) {
    const double fy = ky <= m / 2 ? ky : ky - m;
    for (std::int32_t kx = 0; kx < m; ++kx) {
      const double fx = kx <= m / 2 ? kx : kx - m;
      const auto k = static_cast<std::int32_t>(std::sqrt((fx * fx) + (fy * fy)) + 0.5);
      if (k < 1 || k > bins) {
        continue;
      }
      const Complex& c = f[(static_cast<std::size_t>(ky) * side) + static_cast<std::size_t>(kx)];
      const std::size_t b = static_cast<std::size_t>(k) - 1;
      result.power[b] += ((c.re * c.re) + (c.im * c.im)) / norm;
      ++result.modes[b];
    }
  }
  for (std::size_t b = 0; b < result.freq.size(); ++b) {
    result.freq[b] = (static_cast<double>(b) + 1.0) / static_cast<double>(m);
    result.power[b] /= static_cast<double>(result.modes[b]);
  }
  return result;
}

core::Expected<double> fit_spectral_exponent(const RadialSpectrum& spectrum,
                                             const SpectralFitParams& params) noexcept {
  const std::size_t bins = spectrum.freq.size();
  if (spectrum.power.size() != bins || spectrum.modes.size() != bins) {
    return core::make_error(core::StatusCode::invalid_argument,
                            "fit_spectral_exponent: freq, power and modes differ in size");
  }
  if (bins == 0) {
    return core::make_error(core::StatusCode::invalid_argument,
                            "fit_spectral_exponent: empty spectrum");
  }
  double lo = params.f_min;
  double hi = params.f_max;
  if (lo == 0.0 && hi == 0.0) {
    lo = 2.0 * spectrum.freq.front();
    hi = 0.5 * spectrum.freq.back();
  }
  if (!(lo <= hi)) {
    return core::make_error(core::StatusCode::invalid_argument,
                            "fit_spectral_exponent: f_min must not exceed f_max");
  }
  double sx = 0.0;
  double sy = 0.0;
  std::int64_t count = 0;
  const auto usable = [&](std::size_t b) {
    const double f = spectrum.freq[b];
    return f > 0.0 && f >= lo && f <= hi && spectrum.power[b] > 0.0;
  };
  for (std::size_t b = 0; b < bins; ++b) {
    if (usable(b)) {
      sx += std::log(spectrum.freq[b]);
      sy += std::log(spectrum.power[b]);
      ++count;
    }
  }
  if (count < 2) {
    return core::make_error(core::StatusCode::invalid_argument,
                            "fit_spectral_exponent: fewer than two bins in the band");
  }
  const double mx = sx / static_cast<double>(count);
  const double my = sy / static_cast<double>(count);
  double sxx = 0.0;
  double sxy = 0.0;
  for (std::size_t b = 0; b < bins; ++b) {
    if (usable(b)) {
      const double dx = std::log(spectrum.freq[b]) - mx;
      sxx += dx * dx;
      sxy += dx * (std::log(spectrum.power[b]) - my);
    }
  }
  if (!(sxx > 0.0)) {
    return core::make_error(core::StatusCode::invalid_argument,
                            "fit_spectral_exponent: the band holds a single frequency");
  }
  return -sxy / sxx;
}

}  // namespace wg::num
