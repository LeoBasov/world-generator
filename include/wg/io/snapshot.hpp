#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

#include "wg/core/status.hpp"
#include "wg/core/world_state.hpp"
#include "wg/generated/gen_params.hpp"
#include "wg/io/world_file.hpp"
#include "wg/pipeline/snapshot.hpp"

namespace wg::io {

/// Which stages a SnapshotWriter keeps (wgcli --snapshots none|final|all, plan 6.7).
enum class SnapshotPolicy : std::uint8_t {
  none,   ///< nothing
  final,  ///< the finished world only (the last stage of the successful attempt)
  all,    ///< every completed stage of every attempt, rejected attempts included
};

/// Snapshot hook that writes world files (plan section 6.5: a snapshot is a .wgw whose
/// provenance names the last completed stage). Files go to directory, created on first use, as
/// "a<attempt>_<index>_<stage>.wgw" with a two-digit stage index, e.g. "a0_03_c04_rivers.wgw",
/// so a directory listing sorts them in run order. params must be the parameters of the run
/// and outlive the writer.
class SnapshotWriter final : public pipeline::SnapshotHook {
 public:
  SnapshotWriter(std::filesystem::path directory, const params::GenParams& params,
                 SnapshotPolicy policy, WriteOptions options = {});

  /// Writes the state if the policy keeps this stage. Errors as write_world_file, plus io_error
  /// if the directory cannot be created; the runner then stops with that status.
  core::Status on_stage(const core::WorldState& state,
                        const pipeline::SnapshotInfo& info) noexcept override;

  /// Files written so far, in order.
  [[nodiscard]] const std::vector<std::filesystem::path>& written() const noexcept {
    return written_;
  }

  /// File name of a snapshot, e.g. snapshot_name(info) == "a0_03_c04_rivers.wgw".
  [[nodiscard]] static std::filesystem::path snapshot_name(const pipeline::SnapshotInfo& info);

 private:
  std::filesystem::path directory_;
  const params::GenParams* params_;
  SnapshotPolicy policy_;
  WriteOptions options_;
  std::vector<std::filesystem::path> written_;
};

}  // namespace wg::io
