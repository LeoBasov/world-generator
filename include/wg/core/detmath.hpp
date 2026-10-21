#pragma once

#include <cstdint>

namespace wg::core {

// Deterministic elementary functions. std::exp and std::log come from the platform's libm,
// whose results differ in the last bit between glibc, MSVC and Apple. These are ports of the
// FreeBSD msun (fdlibm) algorithms that use only IEEE + - * / and bit manipulation. With the
// library's floating-point flags (-ffp-contract=off, /fp:precise) they give the same bits on
// every platform. Errors are below 1 ulp. Use them wherever a result feeds the RNG or a golden
// hash and must not depend on the platform (docs/adr/0002-deterministic-rng.md).

/// e^x. Overflow gives +inf, underflow 0, NaN stays NaN.
double det_exp(double x) noexcept;

/// Natural logarithm. log(+-0) = -inf, log(x < 0) = NaN, log(+inf) = +inf.
double det_log(double x) noexcept;

/// ln(k!), from exact factorials for k < 10 and a Stirling series (absolute error below
/// 1e-13) above.
double log_factorial(std::uint64_t k) noexcept;

}  // namespace wg::core
