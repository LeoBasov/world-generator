#include "wg/core/assert.hpp"

#include <doctest/doctest.h>

#include <string>

namespace {

struct CheckFailure {
  std::string expression;
  std::string file;
  int line;
  std::string message;
};

// Throws instead of returning, so check_failed never reaches abort.
void throwing_handler(const char* expr, const char* file, int line, const char* msg) {
  throw CheckFailure{expr, file, line, msg};
}

// Installs throwing_handler for the lifetime of the guard.
class HandlerGuard {
 public:
  HandlerGuard() noexcept : previous_(wg::core::set_check_failure_handler(&throwing_handler)) {}
  ~HandlerGuard() { wg::core::set_check_failure_handler(previous_); }
  HandlerGuard(const HandlerGuard&) = delete;
  HandlerGuard& operator=(const HandlerGuard&) = delete;
  HandlerGuard(HandlerGuard&&) = delete;
  HandlerGuard& operator=(HandlerGuard&&) = delete;

 private:
  wg::core::CheckFailureHandler previous_;
};

void check_positive(int v) { WG_CHECK(v > 0, "value must be positive"); }

}  // namespace

TEST_CASE("core.assert WG_CHECK passes on a true condition") {
  const HandlerGuard guard;
  CHECK_NOTHROW(check_positive(1));
}

TEST_CASE("core.assert WG_CHECK reports expression, location and message") {
  const HandlerGuard guard;
  try {
    check_positive(0);
    FAIL("WG_CHECK did not fire");
  } catch (const CheckFailure& f) {
    CHECK(f.expression == "v > 0");
    CHECK(f.file.find("test_assert.cpp") != std::string::npos);
    CHECK(f.line > 0);
    CHECK(f.message == "value must be positive");
  }
}

TEST_CASE("core.assert set_check_failure_handler returns the previous handler") {
  const wg::core::CheckFailureHandler original = wg::core::set_check_failure_handler(nullptr);
  CHECK(original != nullptr);
  // nullptr restored the default, which is what was installed before.
  CHECK(wg::core::set_check_failure_handler(original) == original);
}

#ifndef NDEBUG
namespace {
void assert_positive(int v) { WG_ASSERT(v > 0, "value must be positive"); }
}  // namespace

TEST_CASE("core.assert WG_ASSERT fires in debug builds") {
  const HandlerGuard guard;
  CHECK_NOTHROW(assert_positive(1));
  CHECK_THROWS_AS(assert_positive(-1), CheckFailure);
}
#endif
