#pragma once

// Raw field chunks of a world file as numbers, for inspect, compare and export (A10).

#include <cstdint>
#include <vector>

#include "wg/core/expected.hpp"
#include "wg/generated/field_ids.hpp"
#include "wg/io/world_file.hpp"

namespace wg::cli {

/// Field names from --field options; "all" (or none given) selects every field of the reader.
/// Errors: invalid_argument for an unknown name, not_found for a field the file lacks.
[[nodiscard]] core::Expected<std::vector<core::FieldId>> select_fields(
    const std::vector<std::string>& names, const io::WorldReader& reader);

/// Reads one field and converts every value to double. Errors as WorldReader::read_field.
[[nodiscard]] core::Expected<std::vector<double>> read_values(io::WorldReader& reader,
                                                              core::FieldId id);

/// Statistics of the finite values.
struct FieldStats {
  double min = 0.0;
  double max = 0.0;
  double mean = 0.0;
  std::int64_t finite = 0;
  std::int64_t non_finite = 0;
};

[[nodiscard]] FieldStats field_stats(const std::vector<double>& values) noexcept;

}  // namespace wg::cli
