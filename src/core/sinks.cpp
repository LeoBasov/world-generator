#include "wg/core/sinks.hpp"

#include <string_view>

namespace wg::core {

namespace {

class NullLogSink final : public LogSink {
 public:
  void write(LogLevel /*level*/, std::string_view /*message*/) noexcept override {}
  [[nodiscard]] bool enabled(LogLevel /*level*/) const noexcept override { return false; }
};

class NullProgressSink final : public ProgressSink {
 public:
  void report(std::string_view /*scope*/, double /*fraction*/) noexcept override {}
};

}  // namespace

std::string_view to_string(LogLevel level) noexcept {
  switch (level) {
    case LogLevel::debug:
      return "debug";
    case LogLevel::info:
      return "info";
    case LogLevel::warning:
      return "warning";
    case LogLevel::error:
      return "error";
  }
  return "unknown";
}

LogSink& null_log_sink() noexcept {
  static NullLogSink sink;
  return sink;
}

ProgressSink& null_progress_sink() noexcept {
  static NullProgressSink sink;
  return sink;
}

}  // namespace wg::core
