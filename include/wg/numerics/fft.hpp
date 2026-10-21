#pragma once

#include <cstdint>
#include <span>

#include "wg/core/memory.hpp"
#include "wg/core/status.hpp"

namespace wg::num {

// Radix-2 fast Fourier transform for the spectrum diagnostics (radial_power_spectrum). The
// twiddle factors come from std::cos and std::sin, so the last bits of the results may differ
// between platforms: use it for analysis and tests, never for world fields or golden hashes
// (docs/adr/0004-numerics-conventions.md).

/// Complex number as a plain pair, so buffers can come from an Arena.
struct Complex {
  double re;
  double im;
};

/// True if n is a power of two (1, 2, 4, ...).
[[nodiscard]] constexpr bool is_power_of_two(std::uint64_t n) noexcept {
  return n != 0 && (n & (n - 1)) == 0;
}

/// In-place discrete Fourier transform X_k = sum_j x_j exp(-2 pi i j k / N); with inverse, the
/// inverse transform including the factor 1 / N, so a forward and an inverse transform restore
/// the input. Errors: invalid_argument unless the length is a power of two.
[[nodiscard]] core::Status fft(std::span<Complex> data, bool inverse = false) noexcept;

/// In-place 2-D transform of a row-major rows x cols array (both powers of two): every row,
/// then every column. Errors: invalid_argument for a size that is not rows * cols or a side
/// that is not a power of two, out_of_memory for the column buffer.
[[nodiscard]] core::Status fft_2d(
    std::span<Complex> data, std::int32_t rows, std::int32_t cols, bool inverse = false,
    core::MemoryResource& resource = core::default_memory_resource()) noexcept;

}  // namespace wg::num
