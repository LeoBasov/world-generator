#include "wg/core/assert.hpp"

#include <atomic>
#include <cstdio>
#include <cstdlib>

namespace wg::core {

namespace {

void default_check_failure_handler(const char* expr, const char* file, int line, const char* msg) {
  std::fprintf(stderr, "%s:%d: check failed: %s %s\n", file, line, expr, msg);
}

std::atomic<CheckFailureHandler> g_handler{&default_check_failure_handler};

}  // namespace

CheckFailureHandler set_check_failure_handler(CheckFailureHandler handler) noexcept {
  if (handler == nullptr) {
    handler = &default_check_failure_handler;
  }
  return g_handler.exchange(handler);
}

void check_failed(const char* expr, const char* file, int line, const char* msg) {
  g_handler.load()(expr, file, line, msg);
  std::abort();
}

}  // namespace wg::core
