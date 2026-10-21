#include "wg/core/sinks.hpp"

#include <doctest/doctest.h>

#include <initializer_list>
#include <string_view>

#include "support/recording_sinks.hpp"

using wg::core::LogLevel;
using wg::core::LogSink;
using wg::core::ProgressSink;

namespace {

// What a kernel does with an injected sink: skip building messages the sink drops.
int log_if_enabled(LogSink& sink, LogLevel level, std::string_view message) {
  if (!sink.enabled(level)) {
    return 0;
  }
  sink.write(level, message);
  return 1;
}

}  // namespace

TEST_CASE("core.sinks level names") {
  CHECK(to_string(LogLevel::debug) == "debug");
  CHECK(to_string(LogLevel::info) == "info");
  CHECK(to_string(LogLevel::warning) == "warning");
  CHECK(to_string(LogLevel::error) == "error");
}

TEST_CASE("core.sinks null sinks drop everything") {
  LogSink& log = wg::core::null_log_sink();
  CHECK(&log == &wg::core::null_log_sink());
  using enum LogLevel;
  for (const LogLevel level : {debug, info, warning, error}) {
    CHECK_FALSE(log.enabled(level));
    log.write(level, "dropped");
  }
  CHECK(log_if_enabled(log, error, "dropped") == 0);

  ProgressSink& progress = wg::core::null_progress_sink();
  CHECK(&progress == &wg::core::null_progress_sink());
  progress.report("c01_allocate", 0.5);
}

TEST_CASE("core.sinks injected sinks receive messages and reports") {
  wg::test::RecordingLogSink log;
  CHECK(log.enabled(LogLevel::debug));
  CHECK(log_if_enabled(log, LogLevel::warning, "clamped 3 cells") == 1);
  log.write(LogLevel::info, "stage finished");
  const auto messages = log.entries();
  REQUIRE(messages.size() == 2);
  CHECK(messages[0].level == LogLevel::warning);
  CHECK(messages[0].message == "clamped 3 cells");
  CHECK(messages[1].level == LogLevel::info);

  wg::test::RecordingProgressSink progress;
  ProgressSink& sink = progress;
  sink.report("c01_allocate", 0.0);
  sink.report("c01_allocate", 1.0);
  const auto reports = progress.entries();
  REQUIRE(reports.size() == 2);
  CHECK(reports[0].scope == "c01_allocate");
  CHECK(reports[1].fraction == 1.0);
}
