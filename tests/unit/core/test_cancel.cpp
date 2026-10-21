#include "wg/core/cancel.hpp"

#include <doctest/doctest.h>

#include <thread>
#include <type_traits>

using wg::core::CancelFlag;

static_assert(!std::is_copy_constructible_v<CancelFlag>);
static_assert(!std::is_move_constructible_v<CancelFlag>);

TEST_CASE("core.cancel request and reset") {
  CancelFlag flag;
  CHECK_FALSE(flag.requested());
  flag.request();
  CHECK(flag.requested());
  flag.request();
  CHECK(flag.requested());
  flag.reset();
  CHECK_FALSE(flag.requested());
}

TEST_CASE("core.cancel a request from another thread is observed") {
  CancelFlag flag;
  std::thread requester([&flag] { flag.request(); });
  while (!flag.requested()) {
    std::this_thread::yield();
  }
  requester.join();
  CHECK(flag.requested());
}
