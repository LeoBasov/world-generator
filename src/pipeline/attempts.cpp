#include "wg/pipeline/attempts.hpp"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <limits>

namespace wg::pipeline {

double AttemptStatistics::mean_attempts() const noexcept {
  return reports == 0 ? 0.0 : static_cast<double>(attempts) / static_cast<double>(reports);
}

double AttemptStatistics::expected_attempts() const noexcept {
  double product = 1.0;
  for (const StageAttempts& s : stages) {
    product *= s.pass_rate();
  }
  return product > 0.0 ? 1.0 / product : std::numeric_limits<double>::infinity();
}

core::Status add_report(AttemptStatistics& stats, const RunReport& report) noexcept {
  try {
    std::int64_t attempts = 0;
    for (const StageRecord& r : report.stages) {
      attempts = std::max(attempts, std::int64_t{r.attempt} + 1);
      auto it = std::find_if(stats.stages.begin(), stats.stages.end(),
                             [&](const StageAttempts& s) { return s.stage == r.stage; });
      if (it == stats.stages.end()) {
        stats.stages.push_back(StageAttempts{.stage = r.stage, .runs = 0, .rejections = 0});
        it = std::prev(stats.stages.end());
      }
      ++it->runs;
      if (r.kind == StageResult::Kind::reject) {
        ++it->rejections;
      }
    }
    ++stats.reports;
    stats.attempts += attempts;
    return {};
  } catch (...) {
    return core::make_status(core::StatusCode::out_of_memory, "add_report: out of memory");
  }
}

}  // namespace wg::pipeline
