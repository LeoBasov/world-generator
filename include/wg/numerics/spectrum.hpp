#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "wg/core/expected.hpp"
#include "wg/core/grid.hpp"
#include "wg/core/memory.hpp"

namespace wg::num {

// Spectral diagnostics of a field (plan section 10.1). Like fft, the results are for analysis
// and tests only and may differ in the last bits between platforms.

/// Parameters of radial_power_spectrum.
struct SpectrumParams {
  bool hann = true;  ///< apply a 2-D Hann window before the transform
};

/// Radially averaged periodogram. Entry k (k = 1 ... (n - 1) / 2) is the mean power of the
/// Fourier modes whose radius rounds to k.
struct RadialSpectrum {
  std::vector<double> freq;          ///< k / (n - 1), cycles per cell
  std::vector<double> power;         ///< mean |F|^2 / sum(w^2) of the modes in the bin
  std::vector<std::int64_t> modes;   ///< number of modes in the bin
};

/// Radially averaged power spectrum of the (n - 1) x (n - 1) block at the north-west corner of
/// the field (n - 1 is a power of two for every valid grid). The block's mean is removed, the
/// window applied, and the power normalised by the sum of the squared window weights, so white
/// noise of variance s^2 has a flat spectrum at s^2. Errors: invalid_argument for an invalid
/// grid, a span without one value per cell or a non-finite value; out_of_memory.
[[nodiscard]] core::Expected<RadialSpectrum> radial_power_spectrum(
    std::span<const float> z, const core::Grid& grid, const SpectrumParams& params,
    core::MemoryResource& resource = core::default_memory_resource()) noexcept;

/// Frequency band of fit_spectral_exponent. f_min = f_max = 0 selects the default band: all
/// bins except the lowest and the highest octave, [2 f_1, f_last / 2].
struct SpectralFitParams {
  double f_min = 0.0;
  double f_max = 0.0;
};

/// Least-squares fit of log P(f) = c - beta log f over the bins of the band with positive power;
/// returns beta. Errors: invalid_argument for vectors of different sizes, f_min > f_max, or
/// fewer than two usable bins.
[[nodiscard]] core::Expected<double> fit_spectral_exponent(
    const RadialSpectrum& spectrum, const SpectralFitParams& params = {}) noexcept;

}  // namespace wg::num
