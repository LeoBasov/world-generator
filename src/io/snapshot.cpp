#include "wg/io/snapshot.hpp"

#include <exception>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>

namespace wg::io {

SnapshotWriter::SnapshotWriter(std::filesystem::path directory, const params::GenParams& params,
                               SnapshotPolicy policy, WriteOptions options)
    : directory_(std::move(directory)), params_(&params), policy_(policy), options_(options) {}

std::filesystem::path SnapshotWriter::snapshot_name(const pipeline::SnapshotInfo& info) {
  std::string index = std::to_string(info.stage_index);
  if (index.size() < 2) {
    index.insert(0, 2 - index.size(), '0');
  }
  return "a" + std::to_string(info.attempt) + "_" + index + "_" + std::string(info.stage) + ".wgw";
}

core::Status SnapshotWriter::on_stage(const core::WorldState& state,
                                      const pipeline::SnapshotInfo& info) noexcept {
  if (policy_ == SnapshotPolicy::none || (policy_ == SnapshotPolicy::final && !info.final)) {
    return {};
  }
  try {
    std::error_code ec;
    std::filesystem::create_directories(directory_, ec);
    if (ec) {
      return core::make_status(
          core::StatusCode::io_error,
          "cannot create " + directory_.generic_string() + ": " + ec.message());
    }
    std::filesystem::path path = directory_ / snapshot_name(info);
    if (core::Status s = write_world_file(path, state, *params_, options_); !s.ok()) {
      return s;
    }
    written_.push_back(std::move(path));
    return {};
  } catch (const std::exception& e) {
    return core::make_status(core::StatusCode::io_error,
                             std::string("writing a snapshot failed: ") + e.what());
  }
}

}  // namespace wg::io
