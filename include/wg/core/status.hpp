#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace wg::core {

/// Outcome category of a public call (plan section 6.6: mapped to Godot errors by the binding).
enum class StatusCode : std::uint8_t {
  ok,
  invalid_argument,     ///< an argument is malformed or out of its domain
  not_found,            ///< the requested field, layer or file does not exist
  type_mismatch,        ///< the requested value type differs from the stored dtype
  out_of_range,         ///< an index or value lies outside its valid range
  out_of_memory,        ///< an allocation failed or a fixed capacity is exhausted
  already_exists,       ///< the object to create exists already
  failed_precondition,  ///< the object is not in a state that allows the call
  cancelled,            ///< the cancel flag was observed
  io_error,             ///< reading or writing a file or buffer failed
  parse_error,          ///< input data is not well formed
  unsupported_version,  ///< input data has a format version this library cannot read
  internal,             ///< a bug: an invariant of the library does not hold
};

/// Number of status codes.
inline constexpr std::size_t kStatusCodeCount = 13;

/// Name of a status code, e.g. "not_found".
std::string_view to_string(StatusCode code) noexcept;

/// Result of a public call that returns no value: a code plus a human-readable message.
/// A default-constructed Status is ok.
struct Status {
  StatusCode code = StatusCode::ok;
  std::string message;

  [[nodiscard]] bool ok() const noexcept { return code == StatusCode::ok; }

  friend bool operator==(const Status&, const Status&) = default;
};

/// Status with the given code and message.
Status make_status(StatusCode code, std::string_view message) noexcept;

/// "code: message", or "ok" for a successful status.
std::string to_string(const Status& status) noexcept;

}  // namespace wg::core
