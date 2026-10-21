// Minimal worldgen-core program: creates a world state, writes a field and reads it back.
// Grows into a full generate-and-inspect example as the pipeline API lands (A08, A23).

#include <cstdint>
#include <cstdio>

#include "wg/wg.hpp"

int main() {
  const wg::core::Version v = wg::core::version();
  std::printf("worldgen-core %d.%d.%d\n", v.major, v.minor, v.patch);

  using wg::core::FieldId;
  auto state = wg::core::WorldState::create(wg::core::Grid{.n = 33, .dx_m = 1000.0, .hu_m = 10.0});
  if (!state) {
    std::printf("error: %s\n", wg::core::to_string(state.error()).c_str());
    return 1;
  }

  auto elevation = state->write<FieldId::elevation>();
  if (!elevation) {
    std::printf("error: %s\n", wg::core::to_string(elevation.error()).c_str());
    return 1;
  }
  const wg::core::Grid& g = state->grid();
  for (std::int32_t y = 0; y < g.n; ++y) {
    for (std::int32_t x = 0; x < g.n; ++x) {
      (*elevation)(x, y) = 100.0F + static_cast<float>(x + y);
    }
  }

  const auto read = state->read<FieldId::elevation>();
  if (!read) {
    std::printf("error: %s\n", wg::core::to_string(read.error()).c_str());
    return 1;
  }
  const double centre = (*read)(g.n / 2, g.n / 2);
  std::printf("grid %d x %d, elevation at the centre %.1f\n", g.n, g.n, centre);
  std::printf("%zu bytes of field storage\n", state->bytes_owned());
  return 0;
}
