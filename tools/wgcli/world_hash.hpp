#pragma once

// World and stage hashes of wgcli hash (A10, ADR 0008), the values golden tests compare.
//
//   field hash  XXH3-64 of the field's raw little-endian bytes (as stored in a world file chunk)
//   world hash  XXH3-64 of the text "grid <n>\n", then "<field name> <field hash>\n" for every
//               present field in FieldId order, then "layers <XXH3-64 of layers_to_json>\n",
//               hashes as 16 lower-case hex digits
//
// Neither includes metrics, wall times, the provenance or the library version, so the hash of a
// generated state equals the hash of the same world read back from its file.

#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "wg/core/expected.hpp"
#include "wg/core/world_state.hpp"
#include "wg/generated/field_ids.hpp"
#include "wg/io/world_file.hpp"
#include "wg/pipeline/snapshot.hpp"

namespace wg::cli {

struct WorldHashes {
  std::uint64_t world = 0;
  std::uint64_t layers = 0;
  std::vector<std::pair<core::FieldId, std::uint64_t>> fields;  ///< in FieldId order
};

/// "0x" and 16 lower-case hex digits.
[[nodiscard]] std::string hex64(std::uint64_t value);

/// Hashes of a state. Errors: those of io::layers_to_json.
[[nodiscard]] core::Expected<WorldHashes> hash_world(const core::WorldState& state);

/// Hashes of a world file, reading every known field chunk. Errors as WorldReader::read_field
/// and io::layers_to_json.
[[nodiscard]] core::Expected<WorldHashes> hash_world_file(io::WorldReader& reader);

/// Hash of one stage of a run.
struct StageHash {
  std::string stage;
  std::uint32_t attempt = 0;
  std::uint64_t world = 0;
};

/// Snapshot hook that hashes the state after every stage. stages_of(attempt) gives the hashes of
/// one attempt, e.g. of the successful one (provenance().attempt).
class StageHasher final : public pipeline::SnapshotHook {
 public:
  core::Status on_stage(const core::WorldState& state,
                        const pipeline::SnapshotInfo& info) noexcept override;
  [[nodiscard]] std::vector<StageHash> stages_of(std::uint32_t attempt) const;

 private:
  std::vector<StageHash> hashes_;
};

}  // namespace wg::cli
