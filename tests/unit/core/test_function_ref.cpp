#include "wg/core/function_ref.hpp"

#include <doctest/doctest.h>

#include <type_traits>

using wg::core::FunctionRef;

static_assert(std::is_trivially_copyable_v<FunctionRef<void(int)>>);
static_assert(std::is_nothrow_constructible_v<FunctionRef<int(int)>, int (*&)(int)>);

namespace {

int twice(int x) { return 2 * x; }

int apply(FunctionRef<int(int)> f, int x) { return f(x); }

}  // namespace

TEST_CASE("core.function_ref calls lambdas and function pointers") {
  int offset = 10;
  CHECK(apply([offset](int x) { return x + offset; }, 5) == 15);

  int (*fp)(int) = &twice;
  CHECK(apply(fp, 21) == 42);
}

TEST_CASE("core.function_ref refers to the callable, not a copy") {
  int calls = 0;
  auto count = [&calls](int x) {
    ++calls;
    return x;
  };
  const FunctionRef<int(int)> ref = count;
  const FunctionRef<int(int)> copy = ref;
  CHECK(ref(1) == 1);
  CHECK(copy(2) == 2);
  CHECK(calls == 2);

  int sum = 0;
  auto add = [&sum](int x) { sum += x; };
  const FunctionRef<void(int)> sink = add;
  sink(3);
  sink(4);
  CHECK(sum == 7);
}

TEST_CASE("core.function_ref converts the result type") {
  const auto half = [](int x) { return x / 2; };
  const FunctionRef<double(int)> ref = half;
  CHECK(ref(9) == 4.0);
}
