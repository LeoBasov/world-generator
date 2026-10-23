#include "wg/stages/classic.hpp"

#include <array>
#include <memory>
#include <string_view>

#include "stages/classic/classic_stages.hpp"

namespace wg::stages {

namespace {

struct Entry {
  std::string_view id;
  std::unique_ptr<pipeline::Stage> (*make)() noexcept;
};

// In profile order.
constexpr std::array<Entry, 2> kStages = {{
    {kC01Allocate, &make_c01_allocate},
    {kC02Seed, &make_c02_seed},
}};

}  // namespace

core::Status add_classic_stages(pipeline::StageRegistry& registry) noexcept {
  try {
    for (const Entry& e : kStages) {
      if (core::Status s = registry.add(e.id, e.make); !s.ok()) {
        return s;
      }
    }
  } catch (...) {
    return core::make_status(core::StatusCode::out_of_memory, "cannot register classic stages");
  }
  return {};
}

}  // namespace wg::stages
