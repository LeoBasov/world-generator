#include "wg/core/version.hpp"

#include <doctest/doctest.h>

#include <string>

TEST_CASE("core.version matches the CMake project version") {
  const auto v = wg::core::version();
  CHECK(v.major == WG_TEST_VERSION_MAJOR);
  CHECK(v.minor == WG_TEST_VERSION_MINOR);
  CHECK(v.patch == WG_TEST_VERSION_PATCH);
}

TEST_CASE("core.version_string is major.minor.patch") {
  const auto v = wg::core::version();
  const std::string expected =
      std::to_string(v.major) + "." + std::to_string(v.minor) + "." + std::to_string(v.patch);
  CHECK(wg::core::version_string() == expected);
}

TEST_CASE("core.version compares all three parts") {
  const wg::core::Version v{.major = 1, .minor = 2, .patch = 3};
  CHECK(v == wg::core::Version{.major = 1, .minor = 2, .patch = 3});
  CHECK(v != wg::core::Version{.major = 1, .minor = 2, .patch = 4});
  CHECK(v != wg::core::Version{.major = 0, .minor = 2, .patch = 3});
}
