#pragma once

#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "wg/core/sinks.hpp"

namespace wg::test {

/// LogSink that keeps every message; thread-safe.
class RecordingLogSink final : public wg::core::LogSink {
 public:
  struct Entry {
    wg::core::LogLevel level;
    std::string message;
  };

  void write(wg::core::LogLevel level, std::string_view message) noexcept override {
    const std::scoped_lock lock(mutex_);
    entries_.push_back(Entry{.level = level, .message = std::string(message)});
  }

  [[nodiscard]] std::vector<Entry> entries() const {
    const std::scoped_lock lock(mutex_);
    return entries_;
  }

 private:
  mutable std::mutex mutex_;
  std::vector<Entry> entries_;
};

/// ProgressSink that keeps every report; thread-safe.
class RecordingProgressSink final : public wg::core::ProgressSink {
 public:
  struct Entry {
    std::string scope;
    double fraction;
  };

  void report(std::string_view scope, double fraction) noexcept override {
    const std::scoped_lock lock(mutex_);
    entries_.push_back(Entry{.scope = std::string(scope), .fraction = fraction});
  }

  [[nodiscard]] std::vector<Entry> entries() const {
    const std::scoped_lock lock(mutex_);
    return entries_;
  }

 private:
  mutable std::mutex mutex_;
  std::vector<Entry> entries_;
};

}  // namespace wg::test
