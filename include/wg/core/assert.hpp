#pragma once

namespace wg::core {

/// Called when a WG_CHECK or WG_ASSERT fails with the expression text, its location and the
/// message (may be empty, never null).
using CheckFailureHandler = void (*)(const char* expr, const char* file, int line, const char* msg);

/// Installs a handler for failed checks and returns the previous one; nullptr restores the
/// default, which writes one line to stderr. After the handler returns, the process aborts.
/// This is a fatal diagnostic for broken invariants, not logging (use the LogSink for that).
CheckFailureHandler set_check_failure_handler(CheckFailureHandler handler) noexcept;

/// Reports a failed check through the handler, then aborts. Used by the macros below.
[[noreturn]] void check_failed(const char* expr, const char* file, int line, const char* msg);

}  // namespace wg::core

/// Invariant checked in every build type; aborts with a message if it does not hold.
#define WG_CHECK(cond, msg) \
  ((cond) ? static_cast<void>(0) : ::wg::core::check_failed(#cond, __FILE__, __LINE__, msg))

/// Invariant checked only in debug builds (NDEBUG not defined).
#ifdef NDEBUG
#define WG_ASSERT(cond, msg) static_cast<void>(sizeof((cond) ? 1 : 0))
#else
#define WG_ASSERT(cond, msg) WG_CHECK(cond, msg)
#endif
