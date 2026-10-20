#pragma once

#include <string_view>

namespace wg::core {

/// Library version, following SemVer. Written into every world file.
struct Version {
  int major;
  int minor;
  int patch;
};

/// Version of the linked worldgen_core library.
Version version() noexcept;

/// Version as "major.minor.patch".
std::string_view version_string() noexcept;

}  // namespace wg::core
