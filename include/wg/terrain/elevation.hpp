#pragma once

namespace wg::terrain {

// Thresholds of the elevation scale (R4.2).

/// Sea level: cells with z < kSeaLevel are ocean, the others land.
inline constexpr float kSeaLevel = 100.0F;

/// Mountains: cells with z >= kMountainElevation. Test rivers start on their edge (plan 7.1).
inline constexpr float kMountainElevation = 300.0F;

/// Top of the elevation scale, the height of a peak.
inline constexpr float kMaxElevation = 400.0F;

}  // namespace wg::terrain
