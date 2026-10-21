#include "json.hpp"

#include <cstddef>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>

namespace wg::io {

core::Expected<Json> parse_json(std::string_view text) noexcept {
  try {
    return Json::parse(text.begin(), text.end());
  } catch (const Json::parse_error& e) {
    return core::make_error(core::StatusCode::parse_error,
                            "JSON parse error at byte " + std::to_string(e.byte) + ": " + e.what());
  } catch (const std::exception& e) {
    return core::make_error(core::StatusCode::parse_error,
                            std::string("JSON parse error: ") + e.what());
  }
}

core::Expected<std::string> read_text_file(const std::filesystem::path& path) noexcept {
  try {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
      return core::make_error(core::StatusCode::io_error, "cannot open " + path.generic_string());
    }
    std::ostringstream text;
    text << in.rdbuf();
    if (in.bad()) {
      return core::make_error(core::StatusCode::io_error, "cannot read " + path.generic_string());
    }
    return std::move(text).str();
  } catch (const std::exception& e) {
    return core::make_error(core::StatusCode::io_error,
                            std::string("cannot read file: ") + e.what());
  }
}

core::Expected<std::string> dump_json(const Json& json, int indent) noexcept {
  try {
    std::string text = json.dump(indent, ' ', false, Json::error_handler_t::replace);
    text += '\n';
    return text;
  } catch (const std::exception& e) {
    return core::make_error(core::StatusCode::internal,
                            std::string("JSON serialisation failed: ") + e.what());
  }
}

std::string_view json_type_name(const Json& json) noexcept {
  if (json.is_boolean()) {
    return "boolean";
  }
  if (json.is_number_integer()) {
    return "integer";
  }
  if (json.is_number()) {
    return "number";
  }
  if (json.is_string()) {
    return "string";
  }
  if (json.is_array()) {
    return "array";
  }
  if (json.is_object()) {
    return "object";
  }
  return "null";
}

std::string json_child_path(std::string_view parent, std::string_view key) {
  std::string path(parent);
  path += '.';
  path += key;
  return path;
}

std::string json_index_path(std::string_view parent, std::size_t index) {
  std::string path(parent);
  path += '[';
  path += std::to_string(index);
  path += ']';
  return path;
}

}  // namespace wg::io
