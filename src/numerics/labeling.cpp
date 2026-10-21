#include "wg/numerics/labeling.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

#include "common.hpp"

namespace wg::num {

namespace {

// Neighbours that precede a cell in row-major order, i.e. visited before it.
constexpr std::array<core::Dir8, 2> kEarlierD4 = {core::Dir8::w, core::Dir8::n};
constexpr std::array<core::Dir8, 4> kEarlierD8 = {core::Dir8::w, core::Dir8::nw, core::Dir8::n,
                                                  core::Dir8::ne};

}  // namespace

core::Expected<UnionFind> UnionFind::create(std::size_t count,
                                            core::MemoryResource& resource) noexcept {
  if (count == 0 || count > kMaxCount) {
    return core::make_error(core::StatusCode::invalid_argument,
                            "union-find: count must lie in [1, 2^31 - 1]");
  }
  core::Expected<core::Arena> arena =
      core::Arena::create(2 * detail::arena_bytes<std::int32_t>(count), resource);
  if (!arena) {
    return core::make_error(std::move(arena.error()));
  }
  core::Expected<std::span<std::int32_t>> parent = arena->allocate<std::int32_t>(count);
  core::Expected<std::span<std::int32_t>> size = arena->allocate<std::int32_t>(count);
  if (!parent || !size) {
    return core::make_error(core::StatusCode::out_of_memory, "union-find: no memory");
  }
  for (std::size_t i = 0; i < count; ++i) {
    (*parent)[i] = static_cast<std::int32_t>(i);
    (*size)[i] = 1;
  }
  return UnionFind(std::move(*arena), *parent, *size);
}

UnionFind::UnionFind(core::Arena arena, std::span<std::int32_t> parent,
                     std::span<std::int32_t> size) noexcept
    : arena_(std::move(arena)), parent_(parent), size_(size) {}

std::int32_t UnionFind::find(std::int32_t i) noexcept {
  while (parent_[static_cast<std::size_t>(i)] != i) {
    const std::int32_t grandparent =
        parent_[static_cast<std::size_t>(parent_[static_cast<std::size_t>(i)])];
    parent_[static_cast<std::size_t>(i)] = grandparent;
    i = grandparent;
  }
  return i;
}

std::int32_t UnionFind::unite(std::int32_t a, std::int32_t b) noexcept {
  std::int32_t ra = find(a);
  std::int32_t rb = find(b);
  if (ra == rb) {
    return ra;
  }
  if (rb < ra) {
    std::swap(ra, rb);
  }
  parent_[static_cast<std::size_t>(rb)] = ra;
  size_[static_cast<std::size_t>(ra)] += size_[static_cast<std::size_t>(rb)];
  return ra;
}

std::int32_t UnionFind::size_of(std::int32_t i) noexcept {
  return size_[static_cast<std::size_t>(find(i))];
}

core::Expected<std::int32_t> label_components(std::span<const std::uint8_t> mask,
                                              const core::Grid& grid, Connectivity connectivity,
                                              std::span<std::int32_t> labels,
                                              core::MemoryResource& resource) noexcept {
  core::Status status = detail::first_error(
      {core::validate(grid),
       detail::check_field(mask, grid, "label_components: mask must have one value per cell"),
       detail::check_field(labels, grid, "label_components: labels must have one value per cell")});
  if (!status.ok()) {
    return core::make_error(std::move(status));
  }
  const auto cells = static_cast<std::size_t>(grid.cells());
  core::Expected<UnionFind> sets = UnionFind::create(cells, resource);
  if (!sets) {
    return core::make_error(std::move(sets.error()));
  }
  std::span<const core::Dir8> earlier = kEarlierD8;
  if (connectivity == Connectivity::d4) {
    earlier = kEarlierD4;
  }
  // Pass 1: join every mask cell with its mask neighbours visited before it.
  for (std::size_t i = 0; i < cells; ++i) {
    if (mask[i] == 0) {
      continue;
    }
    for (const core::Dir8 d : earlier) {
      const std::int64_t j = grid.neighbor(static_cast<std::int64_t>(i), d);
      if (j != core::kNoCell && mask[static_cast<std::size_t>(j)] != 0) {
        sets->unite(static_cast<std::int32_t>(i), static_cast<std::int32_t>(j));
      }
    }
  }
  // Pass 2: a root is its component's first cell in row-major order, so components are
  // numbered when their root is reached and later cells copy the root's label.
  std::int32_t count = 0;
  for (std::size_t i = 0; i < cells; ++i) {
    if (mask[i] == 0) {
      labels[i] = 0;
      continue;
    }
    const std::int32_t root = sets->find(static_cast<std::int32_t>(i));
    labels[i] =
        static_cast<std::size_t>(root) == i ? ++count : labels[static_cast<std::size_t>(root)];
  }
  return count;
}

core::Expected<std::vector<std::int64_t>> component_sizes(std::span<const std::int32_t> labels,
                                                          std::int32_t count) noexcept {
  if (count < 0) {
    return core::make_error(core::StatusCode::invalid_argument,
                            "component_sizes: count must not be negative");
  }
  std::vector<std::int64_t> sizes;
  try {
    sizes.assign(static_cast<std::size_t>(count) + 1, 0);
  } catch (...) {
    return core::make_error(core::StatusCode::out_of_memory, "component_sizes: no memory");
  }
  for (const std::int32_t label : labels) {
    if (label < 0 || label > count) {
      return core::make_error(core::StatusCode::invalid_argument,
                              "component_sizes: label outside [0, count]");
    }
    ++sizes[static_cast<std::size_t>(label)];
  }
  return sizes;
}

}  // namespace wg::num
