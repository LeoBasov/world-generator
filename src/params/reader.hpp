#pragma once

// Internal helpers that read checked values out of a parsed JSON document and record every
// problem as a ValidationIssue with its JSON path (A07). Nothing here throws: values are read
// with get<T>() only after their type was checked.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "io/json.hpp"
#include "wg/params/validation.hpp"

namespace wg::params::detail {

/// Number for messages: integers without a fraction, others with up to 15 significant digits,
/// independent of the global locale.
std::string format_number(double value);

void add_error(std::vector<ValidationIssue>& issues, std::string path, std::string message);
void add_warning(std::vector<ValidationIssue>& issues, std::string path, std::string message);

/// "expected <what>, got <type>" at path.
void add_type_error(std::vector<ValidationIssue>& issues, std::string path, std::string_view what,
                    const io::Json& value);

/// A number inside [lo, hi]; with integer, only JSON integers. Records an issue and returns
/// nullopt otherwise.
std::optional<double> read_number(const io::Json& value, const std::string& path, double lo,
                                  double hi, bool integer, std::vector<ValidationIssue>& issues);

/// Reads the members of one JSON object. Each accessor marks its key as known; finish()
/// reports the keys no accessor asked for, except "$schema". A required key that is missing
/// is an error; a missing optional one yields nullopt (or nullptr) without an issue.
class ObjectReader {
 public:
  ObjectReader(const io::Json& object, std::string path, std::vector<ValidationIssue>& issues);

  [[nodiscard]] const std::string& path() const noexcept { return path_; }
  [[nodiscard]] std::string child(std::string_view key) const;

  std::optional<std::string> string(std::string_view key, bool required = true);
  std::optional<double> number(std::string_view key, double lo, double hi, bool required = true);
  std::optional<std::int64_t> integer(std::string_view key, std::int64_t lo, std::int64_t hi,
                                      bool required = true);
  std::optional<bool> boolean(std::string_view key, bool required = true);
  /// One of the labels.
  std::optional<std::string> label(std::string_view key,
                                   const std::vector<std::string_view>& labels,
                                   bool required = true);
  /// The array, or nullptr. With size != 0 the array must have exactly that many elements.
  const io::Json* array(std::string_view key, std::size_t size = 0, bool required = true);
  /// The object, or nullptr.
  const io::Json* object(std::string_view key, bool required = true);
  /// [lo, hi] given as a two-element integer array inside [min, max] with lo <= hi.
  std::optional<std::pair<std::int64_t, std::int64_t>> interval(std::string_view key,
                                                                std::int64_t min, std::int64_t max);

  /// Reports unknown keys; call once after the last accessor.
  void finish();

  /// False if the value was not an object (an issue was recorded).
  [[nodiscard]] bool valid() const noexcept { return valid_; }

 private:
  const io::Json* find(std::string_view key, bool required);

  const io::Json& object_;
  std::string path_;
  std::vector<ValidationIssue>& issues_;
  std::vector<std::string> known_;
  bool valid_;
};

}  // namespace wg::params::detail
