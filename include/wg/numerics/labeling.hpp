#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "wg/core/arena.hpp"
#include "wg/core/expected.hpp"
#include "wg/core/grid.hpp"
#include "wg/core/memory.hpp"
#include "wg/core/status.hpp"
#include "wg/numerics/neighborhood.hpp"

namespace wg::num {

/// Disjoint sets over the elements 0 ... count-1 with path halving. unite makes the smaller of
/// the two roots the new root, so every set's root is its smallest element, independent of the
/// order of the unions (requirement R1). The storage comes from a MemoryResource at create.
class UnionFind {
 public:
  /// Largest supported element count.
  static constexpr std::size_t kMaxCount = 0x7FFFFFFF;

  /// count (in [1, kMaxCount]) singleton sets. invalid_argument outside that range,
  /// out_of_memory if the storage cannot be allocated.
  [[nodiscard]] static core::Expected<UnionFind> create(
      std::size_t count, core::MemoryResource& resource = core::default_memory_resource()) noexcept;

  /// Root (smallest element) of the set holding i.
  [[nodiscard]] std::int32_t find(std::int32_t i) noexcept;

  /// Joins the sets of a and b and returns the root of the union.
  std::int32_t unite(std::int32_t a, std::int32_t b) noexcept;

  /// Number of elements in the set holding i.
  [[nodiscard]] std::int32_t size_of(std::int32_t i) noexcept;

  [[nodiscard]] std::size_t count() const noexcept { return parent_.size(); }

 private:
  UnionFind(core::Arena arena, std::span<std::int32_t> parent,
            std::span<std::int32_t> size) noexcept;

  core::Arena arena_;
  std::span<std::int32_t> parent_;
  std::span<std::int32_t> size_;
};

/// Labels the connected components of the mask cells (mask_i != 0) under D4 or D8 adjacency:
/// labels 1 ... K, numbered in row-major order of each component's first cell, and 0 outside
/// the mask. Returns K. Errors: invalid_argument for an invalid grid or a span without one value
/// per cell, out_of_memory for the union-find storage.
[[nodiscard]] core::Expected<std::int32_t> label_components(
    std::span<const std::uint8_t> mask, const core::Grid& grid, Connectivity connectivity,
    std::span<std::int32_t> labels,
    core::MemoryResource& resource = core::default_memory_resource()) noexcept;

/// Cells per label: entry k is the number of cells with label k for k in [0, count], entry 0
/// being the background. Errors: invalid_argument for a negative count or a label outside
/// [0, count], out_of_memory.
[[nodiscard]] core::Expected<std::vector<std::int64_t>> component_sizes(
    std::span<const std::int32_t> labels, std::int32_t count) noexcept;

}  // namespace wg::num
