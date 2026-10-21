#include "wg/params/load.hpp"

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
#include "reader.hpp"

namespace wg::params {

namespace {

using detail::add_error;
using detail::add_type_error;
using detail::read_number;

// True if some parameter lies below the object at path.
bool is_group(std::string_view path) noexcept {
  for (const ParamDescriptor& d : kParamDescriptors) {
    if (d.path.size() > path.size() && d.path.substr(0, path.size()) == path &&
        d.path[path.size()] == '.') {
      return true;
    }
  }
  return false;
}

std::string_view kind_name(ParamKind kind) noexcept {
  switch (kind) {
    case ParamKind::real:
      return "numbers";
    case ParamKind::integer:
      return "integers";
    case ParamKind::boolean:
      return "booleans";
    case ParamKind::enumeration:
      return "strings";
  }
  return "values";
}

std::optional<double> read_scalar(const io::Json& v, const ParamDescriptor& d,
                                  const std::string& path, std::vector<ValidationIssue>& issues) {
  switch (d.kind) {
    case ParamKind::real:
      return read_number(v, path, d.min, d.max, false, issues);
    case ParamKind::integer:
      return read_number(v, path, d.min, d.max, true, issues);
    case ParamKind::boolean:
      if (!v.is_boolean()) {
        add_type_error(issues, path, "boolean", v);
        return std::nullopt;
      }
      return v.get<bool>() ? 1.0 : 0.0;
    case ParamKind::enumeration: {
      if (!v.is_string()) {
        add_type_error(issues, path, "string", v);
        return std::nullopt;
      }
      const auto& s = v.get_ref<const std::string&>();
      std::string list;
      for (std::size_t i = 0; i < d.labels.size(); ++i) {
        if (s == d.labels[i]) {
          return static_cast<double>(i);
        }
        list += i == 0 ? "" : ", ";
        list += d.labels[i];
      }
      add_error(issues, path, "\"" + s + "\" is not one of " + list);
      return std::nullopt;
    }
  }
  return std::nullopt;
}

void read_leaf(const io::Json& v, const ParamDescriptor& d, GenParams& params,
               std::vector<ValidationIssue>& issues) {
  const std::string path(d.path);
  if (d.count == 0) {
    if (const std::optional<double> x = read_scalar(v, d, path, issues)) {
      set_param(params, d, 0, *x);
    }
    return;
  }
  if (!v.is_array()) {
    add_type_error(issues, path,
                   "array of " + std::to_string(d.count) + " " + std::string(kind_name(d.kind)), v);
    return;
  }
  if (v.size() != d.count) {
    add_error(issues, path,
              "expected " + std::to_string(d.count) + " elements, got " + std::to_string(v.size()));
    return;
  }
  for (std::size_t i = 0; i < d.count; ++i) {
    if (const std::optional<double> x =
            read_scalar(v[i], d, io::json_index_path(path, i), issues)) {
      set_param(params, d, i, *x);
    }
  }
}

bool is_document_key(std::string_view key) noexcept {
  return key == "$schema" || key == "schema_version" || key == "seed" || key == "profile";
}

void read_group(const io::Json& object, const std::string& path, bool root, GenParams& params,
                std::vector<ValidationIssue>& issues) {
  for (const auto& item : object.items()) {
    const std::string& key = item.key();
    if (root && is_document_key(key)) {
      continue;
    }
    const std::string child = io::json_child_path(path, key);
    // Keys never contain the path separators, so "a.b" cannot address a nested parameter.
    const bool plain = key.find_first_of(".[") == std::string::npos;
    if (const ParamDescriptor* d = plain ? find_param(child) : nullptr) {
      read_leaf(item.value(), *d, params, issues);
    } else if (plain && is_group(child)) {
      if (item.value().is_object()) {
        read_group(item.value(), child, false, params, issues);
      } else {
        add_type_error(issues, child, "object", item.value());
      }
    } else {
      add_error(issues, child, "unknown parameter");
    }
  }
}

bool is_profile_id(std::string_view s) noexcept {
  if (s.empty() || s[0] < 'a' || s[0] > 'z') {
    return false;
  }
  for (char c : s) {
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) {
      return false;
    }
  }
  return true;
}

// Document keys: schema_version (required), seed and profile (optional), $schema (ignored).
core::Status read_document_keys(const io::Json& root, ParamsDocument& doc,
                                std::vector<ValidationIssue>& issues) {
  const auto version = root.find("schema_version");
  if (version == root.end()) {
    add_error(issues, "$.schema_version", "missing required key");
  } else if (!version->is_number_integer()) {
    add_type_error(issues, "$.schema_version", "integer", *version);
  } else if (version->is_number_unsigned() &&
             version->get<std::uint64_t>() > static_cast<std::uint64_t>(kParamsSchemaVersion)) {
    return core::make_status(
        core::StatusCode::unsupported_version,
        "parameter schema_version " + std::to_string(version->get<std::uint64_t>()) +
            " is newer than the supported version " + std::to_string(kParamsSchemaVersion));
  } else if (version->get<std::int64_t>() != kParamsSchemaVersion) {
    // Versions below the current one would be migrated here; version 1 is the first.
    add_error(issues, "$.schema_version",
              std::to_string(version->get<std::int64_t>()) + " is not a parameter schema version");
  }

  if (const auto schema = root.find("$schema"); schema != root.end() && !schema->is_string()) {
    add_type_error(issues, "$.$schema", "string", *schema);
  }
  if (const auto seed = root.find("seed"); seed != root.end()) {
    if (seed->is_number_unsigned()) {
      doc.seed = seed->get<std::uint64_t>();
    } else if (seed->is_number_integer()) {
      add_error(issues, "$.seed",
                std::to_string(seed->get<std::int64_t>()) + " is below the minimum 0");
    } else {
      add_type_error(issues, "$.seed", "integer", *seed);
    }
  }
  if (const auto profile = root.find("profile"); profile != root.end()) {
    if (!profile->is_string()) {
      add_type_error(issues, "$.profile", "string", *profile);
    } else if (!is_profile_id(profile->get_ref<const std::string&>())) {
      add_error(issues, "$.profile",
                "\"" + profile->get<std::string>() + "\" is not a profile id ([a-z][a-z0-9_]*)");
    } else {
      doc.profile = profile->get<std::string>();
    }
  }
  return {};
}

io::Json scalar_json(const GenParams& params, const ParamDescriptor& d, std::size_t i) {
  const double v = get_param(params, d, i);
  switch (d.kind) {
    case ParamKind::real:
      return v;
    case ParamKind::integer:
      return static_cast<std::int64_t>(v);
    case ParamKind::boolean:
      return v != 0.0;
    case ParamKind::enumeration:
      return std::string(param_label(params, d));
  }
  return nullptr;
}

std::string error_lines(const std::vector<ValidationIssue>& issues) {
  std::string text = "invalid parameters:";
  for (const ValidationIssue& i : issues) {
    if (i.severity == Severity::error) {
      text += "\n" + i.path + ": " + i.message;
    }
  }
  return text;
}

}  // namespace

core::Expected<ParamsReadResult> read_params(std::string_view json) noexcept {
  core::Expected<io::Json> root = io::parse_json(json);
  if (!root) {
    return core::make_error(std::move(root.error()));
  }
  try {
    ParamsReadResult result;
    if (!root->is_object()) {
      add_type_error(result.issues, "$", "object", *root);
      return result;
    }
    core::Status status = read_document_keys(*root, result.document, result.issues);
    if (!status.ok()) {
      return core::make_error(std::move(status));
    }
    read_group(*root, "$", true, result.document.params, result.issues);
    if (!has_errors(result.issues)) {
      // Bounds hold for everything read, so validate() adds only cross-field findings.
      std::vector<ValidationIssue> cross = validate(result.document.params);
      result.issues.insert(result.issues.end(), cross.begin(), cross.end());
    }
    return result;
  } catch (const std::exception& e) {
    return core::make_error(core::StatusCode::internal,
                            std::string("reading parameters failed: ") + e.what());
  }
}

core::Expected<ParamsDocument> load_params(std::string_view json) noexcept {
  core::Expected<ParamsReadResult> result = read_params(json);
  if (!result) {
    return core::make_error(std::move(result.error()));
  }
  if (has_errors(result->issues)) {
    return core::make_error(core::StatusCode::invalid_argument, error_lines(result->issues));
  }
  return std::move(result->document);
}

core::Expected<ParamsDocument> load_params_file(const std::filesystem::path& path) noexcept {
  core::Expected<std::string> text = io::read_text_file(path);
  if (!text) {
    return core::make_error(std::move(text.error()));
  }
  return load_params(*text);
}

core::Expected<std::string> to_json(const ParamsDocument& document) noexcept {
  try {
    io::Json root = io::Json::object();
    root["schema_version"] = kParamsSchemaVersion;
    if (document.seed) {
      root["seed"] = *document.seed;
    }
    if (document.profile) {
      root["profile"] = *document.profile;
    }
    for (const ParamDescriptor& d : kParamDescriptors) {
      io::Json* node = &root;
      std::string_view rest = d.path.substr(2);  // after "$."
      for (std::size_t dot = rest.find('.'); dot != std::string_view::npos; dot = rest.find('.')) {
        node = &(*node)[std::string(rest.substr(0, dot))];
        rest.remove_prefix(dot + 1);
      }
      io::Json& leaf = (*node)[std::string(rest)];
      if (d.count == 0) {
        leaf = scalar_json(document.params, d, 0);
      } else {
        leaf = io::Json::array();
        for (std::size_t i = 0; i < d.count; ++i) {
          leaf.push_back(scalar_json(document.params, d, i));
        }
      }
    }
    return io::dump_json(root);
  } catch (const std::exception& e) {
    return core::make_error(core::StatusCode::internal,
                            std::string("writing parameters failed: ") + e.what());
  }
}

}  // namespace wg::params
