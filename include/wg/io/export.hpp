#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "wg/core/expected.hpp"
#include "wg/core/status.hpp"
#include "wg/core/vector_layers.hpp"
#include "wg/core/world_state.hpp"
#include "wg/generated/field_ids.hpp"

namespace wg::io {

// Image and JSON export (plan sections 6.5 and 6.7) for testing, review and interchange; images
// are never read back by the library. Row 0 (the north edge) is the top row of an image.

/// Values mapped to 0 and 65535.
struct Png16Range {
  double lo = 0.0;
  double hi = 1.0;
};

struct Png16Options {
  /// Range to scale; if unset: the registry range of the field if it has both bounds, the
  /// values themselves for u8 and u16 fields (stored unscaled), else the finite min and max of
  /// the data.
  std::optional<Png16Range> range;
};

/// A 16-bit greyscale PNG and the range it was scaled with. The range is also stored in the
/// PNG as tEXt chunks "wg:field", "wg:lo" and "wg:hi".
struct Png16Image {
  std::vector<std::byte> png;
  double lo = 0.0;
  double hi = 0.0;
  /// False if the values were stored as they are (u8 and u16 fields without a range).
  bool scaled = true;
};

/// Grey value of x for range [lo, hi]: round((x - lo) / (hi - lo) * 65535) clamped to
/// [0, 65535], computed in double; 0 for a non-finite x or an empty range (hi <= lo).
[[nodiscard]] std::uint16_t png16_value(double x, double lo, double hi) noexcept;

/// PNG16 of field id. Errors: not_found if the state lacks it, invalid_argument for an explicit
/// range that is not finite with lo < hi, internal if encoding fails.
[[nodiscard]] core::Expected<Png16Image> encode_png16(const core::WorldState& state,
                                                      core::FieldId id,
                                                      const Png16Options& options = {}) noexcept;

/// encode_png16 written to path. Errors as encode_png16, plus io_error.
[[nodiscard]] core::Status export_png16(const std::filesystem::path& path,
                                        const core::WorldState& state, core::FieldId id,
                                        const Png16Options& options = {}) noexcept;

/// True if the library was built with EXR support (CMake option WG_WITH_EXR).
[[nodiscard]] bool exr_supported() noexcept;

/// Single-channel ("Y") float32 EXR of field id with the values unscaled (integer fields
/// converted to float), ZIP-compressed (lossless). Errors: failed_precondition without EXR
/// support, not_found if the state lacks the field, internal if encoding fails.
[[nodiscard]] core::Expected<std::vector<std::byte>> encode_exr(const core::WorldState& state,
                                                                core::FieldId id) noexcept;

/// encode_exr written to path. Errors as encode_exr, plus io_error.
[[nodiscard]] core::Status export_exr(const std::filesystem::path& path,
                                      const core::WorldState& state, core::FieldId id) noexcept;

/// All vector layers as JSON text (2-space indent), the form the world file's metadata uses.
/// Errors: invalid_argument for a non-finite number or an inconsistent river graph.
[[nodiscard]] core::Expected<std::string> layers_to_json(const core::VectorLayers& layers) noexcept;

/// layers_to_json written to path. Errors as layers_to_json, plus io_error.
[[nodiscard]] core::Status export_layers_json(const std::filesystem::path& path,
                                              const core::VectorLayers& layers) noexcept;

}  // namespace wg::io
