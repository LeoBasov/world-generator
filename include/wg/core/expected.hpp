#pragma once

#include <string_view>
#include <utility>

#include <tl/expected.hpp>

#include "wg/core/status.hpp"

namespace wg::core {

/// Value or error, the return type of public calls that produce a value (plan section 6.3).
/// tl::expected is a drop-in for C++23 std::expected. Access the value with * or -> after
/// checking; value() throws and must not be used inside the library.
template <class T, class E = Status>
using Expected = tl::expected<T, E>;

/// Error alternative of an Expected.
using Unexpected = tl::unexpected<Status>;

/// Error with the given code and message, convertible to any Expected<T>.
inline Unexpected make_error(StatusCode code, std::string_view message) noexcept {
  return Unexpected(make_status(code, message));
}

/// Error carrying an existing status, convertible to any Expected<T>.
inline Unexpected make_error(Status status) noexcept { return Unexpected(std::move(status)); }

}  // namespace wg::core
