// Minimal worldgen-core program: links the library and queries its version.
// Grows into a full generate-and-inspect example as the pipeline API lands (A08, A23).

#include <cstdio>

#include "wg/wg.hpp"

int main() {
  const wg::core::Version v = wg::core::version();
  std::printf("worldgen-core %d.%d.%d\n", v.major, v.minor, v.patch);
  return 0;
}
