#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "wg/core/version.hpp"

namespace wg::core {

/// One rejected attempt (plan section 6.4, runner behaviour).
struct RejectionRecord {
  std::uint32_t attempt = 0;
  std::string stage;     ///< stage id
  std::string check_id;  ///< R11 check that failed
  double measured = 0.0;
  double required = 0.0;
};

/// How a world was made; stored in the metadata chunk of every world file (plan section 6.5).
/// Filled by the pipeline runner (A08).
struct Provenance {
  std::string profile;
  std::uint64_t seed = 0;
  std::uint64_t params_hash = 0;
  Version library_version{};
  std::uint32_t attempt = 0;
  std::vector<std::string> completed_stages;  ///< stage ids in run order
  std::vector<RejectionRecord> rejections;

  /// Id of the last completed stage (names a snapshot), empty if none ran yet.
  [[nodiscard]] std::string_view last_completed_stage() const noexcept {
    if (completed_stages.empty()) {
      return {};
    }
    return completed_stages.back();
  }
};

}  // namespace wg::core
