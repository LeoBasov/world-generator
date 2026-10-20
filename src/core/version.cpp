#include "wg/core/version.hpp"

// WG_VERSION_* are defined by CMake from project(VERSION ...).
#ifndef WG_VERSION_STRING
#error "WG_VERSION_* must be defined by the build system"
#endif

namespace wg::core {

Version version() noexcept { return {WG_VERSION_MAJOR, WG_VERSION_MINOR, WG_VERSION_PATCH}; }

std::string_view version_string() noexcept { return WG_VERSION_STRING; }

}  // namespace wg::core
