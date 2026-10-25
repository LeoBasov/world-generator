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
constexpr std::array<Entry, 10> kStages = {{
    {kC01Allocate, &make_c01_allocate},
    {kC02Seed, &make_c02_seed},
    {kC03Fit, &make_c03_fit},
    {kC04Vegetation, &make_c04_vegetation},
    {kC05Shape, &make_c05_shape},
    {kC06Rivers, &make_c06_rivers},
    {kC07Smooth, &make_c07_smooth},
    {kC08Climate, &make_c08_climate},
    {kC09Biomes, &make_c09_biomes},
    {kC10Subsurface, &make_c10_subsurface},
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
