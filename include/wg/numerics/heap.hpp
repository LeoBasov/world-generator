#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <type_traits>
#include <utility>

#include "wg/core/arena.hpp"
#include "wg/core/assert.hpp"
#include "wg/core/expected.hpp"
#include "wg/core/memory.hpp"
#include "wg/core/status.hpp"

namespace wg::num {

/// Entry of an IndexHeap: a priority and the cell it belongs to.
template <class Key>
struct HeapEntry {
  Key key;
  std::int64_t cell;
};

/// Binary min-heap of (key, cell) entries with a fixed capacity, e.g. the open set of a
/// priority-flood (A15). Entries are ordered by key, then by cell index, so the order is total
/// and pops do not depend on the push order (requirement R1). The storage comes from a
/// MemoryResource at create; push and pop never allocate.
template <class Key>
class IndexHeap {
  static_assert(std::is_arithmetic_v<Key>, "heap keys must be arithmetic");

 public:
  using Entry = HeapEntry<Key>;

  /// Heap holding up to capacity (> 0) entries. invalid_argument for capacity 0, out_of_memory
  /// if the storage cannot be allocated.
  [[nodiscard]] static core::Expected<IndexHeap> create(
      std::size_t capacity,
      core::MemoryResource& resource = core::default_memory_resource()) noexcept {
    if (capacity == 0) {
      return core::make_error(core::StatusCode::invalid_argument,
                              "index heap: capacity must be positive");
    }
    if (capacity > std::numeric_limits<std::size_t>::max() / sizeof(Entry) - 1) {
      return core::make_error(core::StatusCode::out_of_memory, "index heap: capacity too large");
    }
    core::Expected<core::Arena> arena =
        core::Arena::create(capacity * sizeof(Entry) + core::Arena::kAlignment, resource);
    if (!arena) {
      return core::make_error(std::move(arena.error()));
    }
    core::Expected<std::span<Entry>> items = arena->allocate<Entry>(capacity);
    if (!items) {
      return core::make_error(std::move(items.error()));
    }
    return IndexHeap(std::move(*arena), *items);
  }

  /// True if a comes out before b: smaller key, or equal keys and smaller cell.
  [[nodiscard]] static constexpr bool before(const Entry& a, const Entry& b) noexcept {
    return a.key < b.key || (!(b.key < a.key) && a.cell < b.cell);
  }

  /// Adds an entry; out_of_memory if the heap is full. The key must not be NaN.
  core::Status push(Key key, std::int64_t cell) noexcept {
    if constexpr (std::is_floating_point_v<Key>) {
      WG_ASSERT(!std::isnan(key), "index heap: NaN key");
    }
    if (size_ == items_.size()) {
      return core::make_status(core::StatusCode::out_of_memory, "index heap: capacity exhausted");
    }
    const Entry e{key, cell};
    std::size_t i = size_++;
    while (i > 0) {
      const std::size_t parent = (i - 1) / 2;
      if (!before(e, items_[parent])) {
        break;
      }
      items_[i] = items_[parent];
      i = parent;
    }
    items_[i] = e;
    return {};
  }

  /// The smallest entry. The heap must not be empty.
  [[nodiscard]] const Entry& top() const noexcept {
    WG_ASSERT(size_ > 0, "index heap: top of an empty heap");
    return items_[0];
  }

  /// Removes and returns the smallest entry. The heap must not be empty.
  Entry pop() noexcept {
    WG_ASSERT(size_ > 0, "index heap: pop of an empty heap");
    const Entry result = items_[0];
    const Entry last = items_[--size_];
    std::size_t i = 0;
    while (true) {
      std::size_t child = (2 * i) + 1;
      if (child >= size_) {
        break;
      }
      if (child + 1 < size_ && before(items_[child + 1], items_[child])) {
        ++child;
      }
      if (!before(items_[child], last)) {
        break;
      }
      items_[i] = items_[child];
      i = child;
    }
    if (size_ > 0) {
      items_[i] = last;
    }
    return result;
  }

  [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
  [[nodiscard]] std::size_t size() const noexcept { return size_; }
  [[nodiscard]] std::size_t capacity() const noexcept { return items_.size(); }

  /// Removes all entries; the storage is kept.
  void clear() noexcept { size_ = 0; }

 private:
  IndexHeap(core::Arena arena, std::span<Entry> items) noexcept
      : arena_(std::move(arena)), items_(items) {}

  core::Arena arena_;
  std::span<Entry> items_;
  std::size_t size_ = 0;
};

}  // namespace wg::num
