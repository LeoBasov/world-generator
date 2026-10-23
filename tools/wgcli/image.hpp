#pragma once

// PNG output of wgcli beyond the library's PNG16 export (A10): grey difference images for
// compare --diff-png and colour-mapped review images for export --colormap.

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>

#include "wg/core/status.hpp"

namespace wg::cli {

/// Writes a 16-bit greyscale PNG of n x n values (row 0 on top). Errors: io_error, internal.
[[nodiscard]] core::Status write_grey16_png(const std::filesystem::path& path, std::int32_t n,
                                            const std::vector<std::uint16_t>& grey);

/// Colour maps of export --colormap.
enum class Colormap : std::uint8_t { grey, terrain, viridis };

[[nodiscard]] std::optional<Colormap> colormap_from_name(std::string_view name) noexcept;

/// RGB of t in [0, 1] (clamped; non-finite t is black).
struct Rgb {
  std::uint8_t r = 0;
  std::uint8_t g = 0;
  std::uint8_t b = 0;
};
[[nodiscard]] Rgb map_color(Colormap map, double t) noexcept;

/// Writes an 8-bit RGB PNG of n x n values mapped from [lo, hi]. Errors: io_error, internal.
[[nodiscard]] core::Status write_colormap_png(const std::filesystem::path& path, std::int32_t n,
                                              const std::vector<double>& values, double lo,
                                              double hi, Colormap map);

}  // namespace wg::cli
