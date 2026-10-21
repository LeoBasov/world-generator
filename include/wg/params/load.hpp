#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "wg/core/expected.hpp"
#include "wg/params/params.hpp"
#include "wg/params/validation.hpp"

namespace wg::params {

// Loading of params.json (plan section 6.5). A file needs "schema_version" and lists only the
// parameters that differ from the defaults; every other key must be a parameter of the schema.
// Loaders take a buffer (exported Godot builds read packed data, plan 12.1) or a path.

/// A parameter file read without refusing it: the document as far as it could be read
/// (refused values keep their defaults) and every issue, errors and warnings.
struct ParamsReadResult {
  ParamsDocument document;
  std::vector<ValidationIssue> issues;
};

/// Reads parameters and reports every issue with its JSON path: wrong types, values out of
/// bounds, unknown keys, then, if those are clean, the cross-field rules of validate().
/// Errors: parse_error for malformed JSON, unsupported_version for a schema_version newer
/// than kParamsSchemaVersion. Every other problem is an issue.
[[nodiscard]] core::Expected<ParamsReadResult> read_params(std::string_view json) noexcept;

/// Reads parameters and refuses them on any error issue: invalid_argument whose message lists
/// one "<path>: <message>" line per error. Warnings do not refuse. Other errors as read_params.
[[nodiscard]] core::Expected<ParamsDocument> load_params(std::string_view json) noexcept;

/// load_params on the contents of a file. Errors: io_error if it cannot be read.
[[nodiscard]] core::Expected<ParamsDocument> load_params_file(
    const std::filesystem::path& path) noexcept;

/// Every parameter of the document as params.json text in schema order (2-space indent), so
/// load_params(to_json(d)) == d. Errors: internal if serialisation fails.
[[nodiscard]] core::Expected<std::string> to_json(const ParamsDocument& document) noexcept;

}  // namespace wg::params
