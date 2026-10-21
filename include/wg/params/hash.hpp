#pragma once

#include <cstdint>

#include "wg/params/params.hpp"

namespace wg::params {

/// Parameter hash: XXH3-64 of a canonical encoding of every parameter value in schema order,
/// together with the parameter paths and the schema version (ADR 0005). It hashes values, not
/// JSON text, so formatting, key order and omitted defaults do not change it, and
/// load_params(to_json(d)) keeps it. -0.0 hashes as +0.0. Seed and profile are not included.
/// Written into every world file (plan section 6.5).
[[nodiscard]] std::uint64_t params_hash(const GenParams& params) noexcept;

}  // namespace wg::params
