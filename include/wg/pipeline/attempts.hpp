#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "wg/core/status.hpp"
#include "wg/pipeline/pipeline.hpp"

namespace wg::pipeline {

// Rejection statistics over run reports (R11, A22). If stage i passes with probability p_i and
// the stages reject independently, a world needs E[attempts] = (prod_i p_i)^-1 attempts. The
// estimate of p_i is conditional on the attempt reaching stage i.

/// Runs of one stage, over every attempt of every report added.
struct StageAttempts {
  std::string stage;
  /// Attempts that reached the stage.
  std::int64_t runs = 0;
  /// Runs that ended in reject (a repaired run counts as passed).
  std::int64_t rejections = 0;

  /// Estimate of p_i: (runs - rejections) / runs; 1 for a stage never reached.
  [[nodiscard]] double pass_rate() const noexcept {
    return runs == 0 ? 1.0 : static_cast<double>(runs - rejections) / static_cast<double>(runs);
  }
};

/// Attempt statistics of one or more runs.
struct AttemptStatistics {
  /// Stages in the order first seen.
  std::vector<StageAttempts> stages;
  /// Reports added.
  std::int64_t reports = 0;
  /// Attempts over all reports (the highest attempt index + 1 per report).
  std::int64_t attempts = 0;

  /// Measured attempts per report; 0 without reports.
  [[nodiscard]] double mean_attempts() const noexcept;
  /// (prod_i pass_rate_i)^-1, infinity if a stage never passed.
  [[nodiscard]] double expected_attempts() const noexcept;
};

/// Adds the stage records of report (of a RunOutput or a FailureReport) to stats. Errors:
/// out_of_memory.
[[nodiscard]] core::Status add_report(AttemptStatistics& stats, const RunReport& report) noexcept;

}  // namespace wg::pipeline
