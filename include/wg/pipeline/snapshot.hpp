#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "wg/core/status.hpp"
#include "wg/core/world_state.hpp"

namespace wg::pipeline {

/// Where a run stands when the snapshot hook is called.
struct SnapshotInfo {
  std::string_view profile;
  std::string_view stage;   ///< id of the stage that just completed
  std::size_t stage_index;  ///< its position in the profile
  std::uint32_t attempt;
  /// True after the last stage of the successful attempt: the state is the finished world.
  bool final;
};

/// Called by the runner after every completed stage (plan section 2: snapshots everywhere).
/// The state's provenance already names the completed stage, so writing it as a .wgw file
/// (A09) gives a snapshot. A hook decides what to keep: wgcli --snapshots none|final|all, the
/// Godot viewer's stage scrubber. Stages of a rejected attempt are reported too (with their
/// attempt); a later attempt starts again at stage 0. Runs on the thread of Pipeline::run.
class SnapshotHook {
 public:
  virtual ~SnapshotHook() = default;

  /// Receives the state after a stage. A non-ok status stops the run with that status.
  virtual core::Status on_stage(const core::WorldState& state,
                                const SnapshotInfo& info) noexcept = 0;

 protected:
  SnapshotHook() = default;
  SnapshotHook(const SnapshotHook&) = default;
  SnapshotHook& operator=(const SnapshotHook&) = default;
  SnapshotHook(SnapshotHook&&) = default;
  SnapshotHook& operator=(SnapshotHook&&) = default;
};

}  // namespace wg::pipeline
