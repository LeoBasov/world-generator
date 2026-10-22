#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "wg/core/expected.hpp"

namespace wg::pipeline {

/// Version of the profile file format (schema/profile.schema.json).
inline constexpr std::int64_t kProfileSchemaVersion = 1;

/// A profile: the ordered stage ids of one generation method (plan sections 2 and 8,
/// data/profiles/). Profiles are data, so a new profile needs no code change.
struct Profile {
  /// Profile id, e.g. "classic"; written into the provenance of every world.
  std::string name;
  /// Stage ids in run order; non-empty, without duplicates.
  std::vector<std::string> stages;

  friend bool operator==(const Profile&, const Profile&) = default;
};

/// Reads a profile file: {"schema_version": 1, "name": "...", "description": "...",
/// "stages": ["c01_allocate", ...]} ("$schema" and "description" optional). Errors:
/// parse_error for malformed JSON, unsupported_version for a newer schema_version,
/// invalid_argument whose message lists one "<path>: <message>" line per problem (missing or
/// unknown keys, wrong types, invalid or repeated ids, no stages). Whether the stages exist and
/// are causally ordered is checked by Pipeline::create.
[[nodiscard]] core::Expected<Profile> load_profile(std::string_view json) noexcept;

/// load_profile on the contents of a file. Errors: io_error if it cannot be read.
[[nodiscard]] core::Expected<Profile> load_profile_file(const std::filesystem::path& path) noexcept;

}  // namespace wg::pipeline
