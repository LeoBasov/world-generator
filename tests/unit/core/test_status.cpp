#include "wg/core/status.hpp"

#include <doctest/doctest.h>

#include <cstddef>
#include <set>
#include <string>
#include <string_view>

using wg::core::make_status;
using wg::core::Status;
using wg::core::StatusCode;

TEST_CASE("core.status default is ok") {
  const Status s;
  CHECK(s.ok());
  CHECK(s.code == StatusCode::ok);
  CHECK(s.message.empty());
  CHECK(wg::core::to_string(s) == "ok");
}

TEST_CASE("core.status make_status keeps code and message") {
  const Status s = make_status(StatusCode::not_found, "field 'x' is absent");
  CHECK_FALSE(s.ok());
  CHECK(s.code == StatusCode::not_found);
  CHECK(s.message == "field 'x' is absent");
  CHECK(wg::core::to_string(s) == "not_found: field 'x' is absent");
  CHECK(s == make_status(StatusCode::not_found, "field 'x' is absent"));
  CHECK_FALSE(s == make_status(StatusCode::not_found, "other"));
}

TEST_CASE("core.status every code has a distinct name") {
  std::set<std::string_view> names;
  for (std::size_t i = 0; i < wg::core::kStatusCodeCount; ++i) {
    const std::string_view name = wg::core::to_string(static_cast<StatusCode>(i));
    CHECK(name != "unknown");
    names.insert(name);
  }
  CHECK(names.size() == wg::core::kStatusCodeCount);
  CHECK(wg::core::to_string(StatusCode::internal) == "internal");
  CHECK(wg::core::to_string(static_cast<StatusCode>(wg::core::kStatusCodeCount)) == "unknown");
}
