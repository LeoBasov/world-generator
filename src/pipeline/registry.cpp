#include "wg/pipeline/registry.hpp"

#include <algorithm>
#include <exception>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace wg::pipeline {

namespace {

// First entry whose id is not less than id; entries are sorted by id.
template <class Entries>
auto lower_bound_id(Entries& entries, std::string_view id) noexcept {
  return std::lower_bound(entries.begin(), entries.end(), id,
                          [](const auto& e, std::string_view key) { return e.id < key; });
}

}  // namespace

bool is_valid_id(std::string_view id) noexcept {
  if (id.empty() || id.front() < 'a' || id.front() > 'z') {
    return false;
  }
  return std::all_of(id.begin(), id.end(), [](char c) {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
  });
}

core::Status StageRegistry::add(std::string_view id, Factory factory) noexcept {
  if (!is_valid_id(id)) {
    return core::make_status(core::StatusCode::invalid_argument,
                             "\"" + std::string(id) + "\" is not a stage id ([a-z][a-z0-9_]*)");
  }
  if (!factory) {
    return core::make_status(core::StatusCode::invalid_argument,
                             "stage " + std::string(id) + ": empty factory");
  }
  const auto it = lower_bound_id(entries_, id);
  if (it != entries_.end() && it->id == id) {
    return core::make_status(core::StatusCode::already_exists,
                             "stage " + std::string(id) + " is registered already");
  }
  try {
    entries_.insert(it, Entry{std::string(id), std::move(factory)});
  } catch (...) {
    return core::make_status(core::StatusCode::out_of_memory,
                             "stage " + std::string(id) + ": cannot register");
  }
  return {};
}

bool StageRegistry::contains(std::string_view id) const noexcept {
  const auto it = lower_bound_id(entries_, id);
  return it != entries_.end() && it->id == id;
}

std::vector<std::string_view> StageRegistry::ids() const noexcept {
  std::vector<std::string_view> ids;
  ids.reserve(entries_.size());
  for (const Entry& e : entries_) {
    ids.emplace_back(e.id);
  }
  return ids;
}

core::Expected<std::unique_ptr<Stage>> StageRegistry::create(std::string_view id) const noexcept {
  const auto it = lower_bound_id(entries_, id);
  if (it == entries_.end() || it->id != id) {
    return core::make_error(core::StatusCode::not_found,
                            "stage " + std::string(id) + " is not registered");
  }
  std::unique_ptr<Stage> stage;
  try {
    stage = it->factory();
  } catch (const std::exception& e) {
    return core::make_error(core::StatusCode::internal,
                            "stage " + std::string(id) + ": factory threw: " + e.what());
  } catch (...) {
    return core::make_error(core::StatusCode::internal,
                            "stage " + std::string(id) + ": factory threw");
  }
  if (stage == nullptr) {
    return core::make_error(core::StatusCode::internal,
                            "stage " + std::string(id) + ": factory returned no stage");
  }
  if (stage->id() != id) {
    return core::make_error(
        core::StatusCode::internal,
        "stage " + std::string(id) + ": factory made a stage with id " + std::string(stage->id()));
  }
  return {std::move(stage)};
}

}  // namespace wg::pipeline
