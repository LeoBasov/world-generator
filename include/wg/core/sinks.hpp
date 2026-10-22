#pragma once

#include <cstdint>
#include <string_view>

namespace wg::core {

/// Severity of a log message.
enum class LogLevel : std::uint8_t {
  debug,    ///< detail for diagnosing the library
  info,     ///< progress of a run, e.g. a stage finished
  warning,  ///< something was repaired or clamped; the run continues
  error,    ///< the run cannot continue; the returned Status carries the same message
};

/// Name of a log level, e.g. "warning".
std::string_view to_string(LogLevel level) noexcept;

/// Destination of log messages (plan section 6.3: the library never writes to stdout or
/// stderr). The CLI writes them to stderr, Phase 2 forwards them to Godot. May be called from
/// worker threads, so implementations must be thread-safe.
class LogSink {
 public:
  virtual ~LogSink() = default;

  /// Receives one message, without a trailing newline.
  virtual void write(LogLevel level, std::string_view message) noexcept = 0;

  /// False if messages of this level are dropped, so callers can skip building them.
  [[nodiscard]] virtual bool enabled(LogLevel /*level*/) const noexcept { return true; }

 protected:
  LogSink() = default;
  LogSink(const LogSink&) = default;
  LogSink& operator=(const LogSink&) = default;
  LogSink(LogSink&&) = default;
  LogSink& operator=(LogSink&&) = default;
};

/// Destination of progress reports (requirement R10: progress visible). May be called from
/// worker threads, so implementations must be thread-safe.
class ProgressSink {
 public:
  virtual ~ProgressSink() = default;

  /// fraction in [0, 1] of the work in scope (a stage id, or "pipeline") is done;
  /// non-decreasing per scope within one pipeline attempt, restarting at 0 with a new attempt.
  virtual void report(std::string_view scope, double fraction) noexcept = 0;

 protected:
  ProgressSink() = default;
  ProgressSink(const ProgressSink&) = default;
  ProgressSink& operator=(const ProgressSink&) = default;
  ProgressSink(ProgressSink&&) = default;
  ProgressSink& operator=(ProgressSink&&) = default;
};

/// Process-wide sink that drops every message; enabled() is false for every level.
LogSink& null_log_sink() noexcept;

/// Process-wide sink that drops every report.
ProgressSink& null_progress_sink() noexcept;

}  // namespace wg::core
