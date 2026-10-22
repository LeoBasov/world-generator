#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "wg/core/expected.hpp"
#include "wg/core/status.hpp"
#include "wg/pipeline/stage.hpp"

namespace wg::pipeline {

/// True for a valid stage or profile id: [a-z][a-z0-9_]*, e.g. "c01_allocate".
[[nodiscard]] bool is_valid_id(std::string_view id) noexcept;

/// Stages a profile may name, by id (plan section 7: stage registry). The registry holds
/// factories, not stages, so every Pipeline gets its own instances. There is no global
/// registry: the library offers functions that add its stages (from A11), and tests build
/// registries of mock stages (R4).
class StageRegistry {
 public:
  /// Creates a stage. Must not throw; a factory that returns nullptr makes create fail.
  using Factory = std::function<std::unique_ptr<Stage>()>;

  /// Registers factory under id. invalid_argument for an id that is not is_valid_id or an empty
  /// factory, already_exists if id is registered.
  core::Status add(std::string_view id, Factory factory) noexcept;

  [[nodiscard]] bool contains(std::string_view id) const noexcept;
  [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }

  /// Registered ids in ascending order.
  [[nodiscard]] std::vector<std::string_view> ids() const noexcept;

  /// A new instance of stage id. not_found if id is not registered; internal if the factory
  /// returns nullptr, throws, or makes a stage whose id() differs from id (the id keys the RNG).
  [[nodiscard]] core::Expected<std::unique_ptr<Stage>> create(std::string_view id) const noexcept;

 private:
  struct Entry {
    std::string id;
    Factory factory;
  };

  std::vector<Entry> entries_;  ///< sorted by id
};

}  // namespace wg::pipeline
