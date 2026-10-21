#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "wg/generated/gen_params.hpp"

namespace wg::params {

/// Errors refuse a parameter set; warnings report a known contradiction that makes every
/// attempt fail a check (plan 16) but are accepted.
enum class Severity : std::uint8_t { error, warning };

/// One finding of loading or validation, located by its JSON path, e.g.
/// "$.fields.elevation.weights[2]".
struct ValidationIssue {
  Severity severity = Severity::error;
  std::string path;
  std::string message;

  friend bool operator==(const ValidationIssue&, const ValidationIssue&) = default;
};

/// Checks every parameter against its bounds and the cross-field rules: field ranges with
/// min <= max inside the field registry range, a positive weight per field, N = 2^n + 1, a
/// power-of-two refinement, ordered bands and min/max pairs, square counts that fit the grid.
/// Warns about the contradictions of the report (peaks, volcanoes, rivers and edge oceans that
/// the field ranges make impossible). Issues come in a fixed order: bounds in schema order,
/// then the cross-field rules.
[[nodiscard]] std::vector<ValidationIssue> validate(const GenParams& params) noexcept;

/// True if any issue is an error.
[[nodiscard]] bool has_errors(std::span<const ValidationIssue> issues) noexcept;

/// One line per issue: "error: <path>: <message>" or "warning: ...".
[[nodiscard]] std::string format_issues(std::span<const ValidationIssue> issues) noexcept;

}  // namespace wg::params
