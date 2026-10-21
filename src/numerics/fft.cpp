#include "wg/numerics/fft.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <utility>

#include "common.hpp"
#include "wg/core/arena.hpp"
#include "wg/core/expected.hpp"

namespace wg::num {

namespace {

constexpr double kPi = std::numbers::pi;

}  // namespace

core::Status fft(std::span<Complex> data, bool inverse) noexcept {
  const std::size_t n = data.size();
  if (!is_power_of_two(n)) {
    return core::make_status(core::StatusCode::invalid_argument,
                             "fft: the length must be a power of two");
  }
  // Bit-reversal permutation.
  for (std::size_t i = 1, j = 0; i < n; ++i) {
    std::size_t bit = n >> 1U;
    for (; (j & bit) != 0; bit >>= 1U) {
      j ^= bit;
    }
    j ^= bit;
    if (i < j) {
      std::swap(data[i], data[j]);
    }
  }
  // Butterflies; each twiddle factor is computed directly rather than by a recurrence.
  const double sign = inverse ? 1.0 : -1.0;
  for (std::size_t len = 2; len <= n; len <<= 1U) {
    const double angle = sign * 2.0 * kPi / static_cast<double>(len);
    const std::size_t half = len / 2;
    for (std::size_t k = 0; k < half; ++k) {
      const double a = angle * static_cast<double>(k);
      const Complex w{std::cos(a), std::sin(a)};
      for (std::size_t s = 0; s < n; s += len) {
        Complex& u = data[s + k];
        Complex& v = data[s + k + half];
        const Complex t{(v.re * w.re) - (v.im * w.im), (v.re * w.im) + (v.im * w.re)};
        v = Complex{u.re - t.re, u.im - t.im};
        u = Complex{u.re + t.re, u.im + t.im};
      }
    }
  }
  if (inverse) {
    const double scale = 1.0 / static_cast<double>(n);
    for (Complex& c : data) {
      c.re *= scale;
      c.im *= scale;
    }
  }
  return {};
}

core::Status fft_2d(std::span<Complex> data, std::int32_t rows, std::int32_t cols, bool inverse,
                    core::MemoryResource& resource) noexcept {
  if (rows <= 0 || cols <= 0 || !is_power_of_two(static_cast<std::uint64_t>(rows)) ||
      !is_power_of_two(static_cast<std::uint64_t>(cols))) {
    return core::make_status(core::StatusCode::invalid_argument,
                             "fft_2d: rows and cols must be powers of two");
  }
  const auto r = static_cast<std::size_t>(rows);
  const auto c = static_cast<std::size_t>(cols);
  if (data.size() != r * c) {
    return core::make_status(core::StatusCode::invalid_argument,
                             "fft_2d: the size must be rows * cols");
  }
  for (std::size_t y = 0; y < r; ++y) {
    core::Status status = fft(data.subspan(y * c, c), inverse);
    if (!status.ok()) {
      return status;
    }
  }
  core::Expected<core::Arena> arena =
      core::Arena::create(detail::arena_bytes<Complex>(r), resource);
  if (!arena) {
    return std::move(arena.error());
  }
  core::Expected<std::span<Complex>> column = arena->allocate<Complex>(r);
  if (!column) {
    return std::move(column.error());
  }
  for (std::size_t x = 0; x < c; ++x) {
    for (std::size_t y = 0; y < r; ++y) {
      (*column)[y] = data[(y * c) + x];
    }
    core::Status status = fft(*column, inverse);
    if (!status.ok()) {
      return status;
    }
    for (std::size_t y = 0; y < r; ++y) {
      data[(y * c) + x] = (*column)[y];
    }
  }
  return {};
}

}  // namespace wg::num
