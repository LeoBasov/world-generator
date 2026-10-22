#include "wg/pipeline/stage.hpp"

#include <string_view>

namespace wg::pipeline {

std::string_view to_string(StageResult::Kind kind) noexcept {
  switch (kind) {
    case StageResult::Kind::ok:
      return "ok";
    case StageResult::Kind::repaired:
      return "repaired";
    case StageResult::Kind::reject:
      return "reject";
    case StageResult::Kind::cancelled:
      return "cancelled";
    case StageResult::Kind::error:
      return "error";
  }
  return "unknown";
}

}  // namespace wg::pipeline
