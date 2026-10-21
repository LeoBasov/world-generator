#pragma once

// Internal JSON layer (not installed). nlohmann/json is used only inside the library; parse and
// dump, the calls that throw, live in json.cpp and turn exceptions into a Status (plan 6.1).
// Callers read a parsed document only through non-throwing accessors: find, is_*, then get<T>
// on a value whose type was checked.

#include <filesystem>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include "wg/core/expected.hpp"

namespace wg::io {

/// JSON document that keeps the key order of the input (and of insertion when written).
using Json = nlohmann::ordered_json;

/// Parses UTF-8 JSON text. Errors: parse_error with the byte offset and nlohmann's message.
core::Expected<Json> parse_json(std::string_view text) noexcept;

/// Reads a whole file as bytes. Errors: io_error naming the path.
core::Expected<std::string> read_text_file(const std::filesystem::path& path) noexcept;

/// Writes JSON text with the given indent and a final newline. Doubles are written with the
/// shortest representation that reads back to the same value. Invalid UTF-8 in strings is
/// replaced by U+FFFD. Errors: internal if serialisation fails.
core::Expected<std::string> dump_json(const Json& json, int indent = 2) noexcept;

/// Name of the JSON type of a value, e.g. "number" or "string", for error messages.
std::string_view json_type_name(const Json& json) noexcept;

/// Path of a value for error messages, "$" for the document: child of an object or array.
std::string json_child_path(std::string_view parent, std::string_view key);
std::string json_index_path(std::string_view parent, std::size_t index);

}  // namespace wg::io
