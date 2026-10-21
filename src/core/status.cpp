#include "wg/core/status.hpp"

#include <string>
#include <string_view>

namespace wg::core {

std::string_view to_string(StatusCode code) noexcept {
  switch (code) {
    case StatusCode::ok:
      return "ok";
    case StatusCode::invalid_argument:
      return "invalid_argument";
    case StatusCode::not_found:
      return "not_found";
    case StatusCode::type_mismatch:
      return "type_mismatch";
    case StatusCode::out_of_range:
      return "out_of_range";
    case StatusCode::out_of_memory:
      return "out_of_memory";
    case StatusCode::already_exists:
      return "already_exists";
    case StatusCode::failed_precondition:
      return "failed_precondition";
    case StatusCode::cancelled:
      return "cancelled";
    case StatusCode::io_error:
      return "io_error";
    case StatusCode::parse_error:
      return "parse_error";
    case StatusCode::unsupported_version:
      return "unsupported_version";
    case StatusCode::internal:
      return "internal";
  }
  return "unknown";
}

Status make_status(StatusCode code, std::string_view message) noexcept {
  return {.code = code, .message = std::string(message)};
}

std::string to_string(const Status& status) noexcept {
  std::string text(to_string(status.code));
  if (!status.message.empty()) {
    text += ": ";
    text += status.message;
  }
  return text;
}

}  // namespace wg::core
