#include "wg/pipeline/profile.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "io/json.hpp"
#include "params/reader.hpp"
#include "wg/params/validation.hpp"
#include "wg/pipeline/registry.hpp"

namespace wg::pipeline {

namespace {

using params::Severity;
using params::ValidationIssue;
using params::detail::add_error;
using params::detail::add_type_error;

std::string error_lines(const std::vector<ValidationIssue>& issues) {
  std::string text = "invalid profile:";
  for (const ValidationIssue& i : issues) {
    if (i.severity == Severity::error) {
      text += "\n" + i.path + ": " + i.message;
    }
  }
  return text;
}

void read_stages(const io::Json& stages, const std::string& path, Profile& profile,
                 std::vector<ValidationIssue>& issues) {
  if (stages.empty()) {
    add_error(issues, path, "a profile needs at least one stage");
    return;
  }
  for (std::size_t i = 0; i < stages.size(); ++i) {
    const std::string item_path = io::json_index_path(path, i);
    const io::Json& v = stages[i];
    if (!v.is_string()) {
      add_type_error(issues, item_path, "string", v);
      continue;
    }
    const auto& id = v.get_ref<const std::string&>();
    if (!is_valid_id(id)) {
      add_error(issues, item_path, "\"" + id + "\" is not a stage id ([a-z][a-z0-9_]*)");
      continue;
    }
    if (std::find(profile.stages.begin(), profile.stages.end(), id) != profile.stages.end()) {
      add_error(issues, item_path, "stage " + id + " is listed twice");
    }
    profile.stages.push_back(id);
  }
}

}  // namespace

core::Expected<Profile> load_profile(std::string_view json) noexcept {
  core::Expected<io::Json> root = io::parse_json(json);
  if (!root) {
    return core::make_error(std::move(root.error()));
  }
  try {
    // find() of a non-object returns end(); the reader below reports the wrong type.
    if (const auto version = root->find("schema_version");
        version != root->end() && version->is_number_unsigned() &&
        version->get<std::uint64_t>() > static_cast<std::uint64_t>(kProfileSchemaVersion)) {
      return core::make_error(
          core::StatusCode::unsupported_version,
          "profile schema_version " + std::to_string(version->get<std::uint64_t>()) +
              " is newer than the supported version " + std::to_string(kProfileSchemaVersion));
    }
    std::vector<ValidationIssue> issues;
    Profile profile;
    params::detail::ObjectReader reader(*root, "$", issues);
    reader.integer("schema_version", kProfileSchemaVersion, kProfileSchemaVersion);
    if (std::optional<std::string> name = reader.string("name")) {
      if (is_valid_id(*name)) {
        profile.name = std::move(*name);
      } else {
        add_error(issues, reader.child("name"),
                  "\"" + *name + "\" is not a profile id ([a-z][a-z0-9_]*)");
      }
    }
    reader.string("description", false);
    if (const io::Json* stages = reader.array("stages")) {
      read_stages(*stages, reader.child("stages"), profile, issues);
    }
    reader.finish();
    if (params::has_errors(issues)) {
      return core::make_error(core::StatusCode::invalid_argument, error_lines(issues));
    }
    return profile;
  } catch (const std::exception& e) {
    return core::make_error(core::StatusCode::internal,
                            std::string("reading the profile failed: ") + e.what());
  }
}

core::Expected<Profile> load_profile_file(const std::filesystem::path& path) noexcept {
  core::Expected<std::string> text = io::read_text_file(path);
  if (!text) {
    return core::make_error(std::move(text.error()));
  }
  return load_profile(*text);
}

}  // namespace wg::pipeline
