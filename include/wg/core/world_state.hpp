#pragma once

#include <cstddef>
#include <span>
#include <utility>

#include "wg/core/dtype.hpp"
#include "wg/core/expected.hpp"
#include "wg/core/field_store.hpp"
#include "wg/core/field_view.hpp"
#include "wg/core/grid.hpp"
#include "wg/core/memory.hpp"
#include "wg/core/provenance.hpp"
#include "wg/core/status.hpp"
#include "wg/core/vector_layers.hpp"
#include "wg/generated/field_ids.hpp"

namespace wg::core {

/// Everything stages exchange: the grid, the raster fields, the vector layers and the
/// provenance (plan section 2: fields are the interface).
///
/// Fields are typed by the registry: read<T> and write<T> refuse a T that differs from the
/// field's dtype (type_mismatch). Writing allocates a zero-filled field on first use; adopt
/// lets a caller (the Godot binding) supply the buffer so results land in its memory.
///
/// Threading: structural calls (write of an absent field, adopt, release, moving the state)
/// must not run concurrently with anything else on the same state. Kernels may read views and
/// write disjoint ranges of a view from several threads.
class WorldState {
 public:
  /// State for a valid grid (see validate(const Grid&)) with no fields; resource must outlive
  /// the state. Replaces the constructor of the plan sketch so an invalid grid is a status.
  [[nodiscard]] static Expected<WorldState> create(
      const Grid& grid, MemoryResource& resource = default_memory_resource()) noexcept;

  WorldState(const WorldState&) = delete;
  WorldState& operator=(const WorldState&) = delete;
  WorldState(WorldState&&) noexcept = default;
  WorldState& operator=(WorldState&&) noexcept = default;
  ~WorldState() = default;

  [[nodiscard]] const Grid& grid() const noexcept { return grid_; }
  [[nodiscard]] bool has(FieldId id) const noexcept { return store_.has(id); }
  [[nodiscard]] bool is_adopted(FieldId id) const noexcept { return store_.is_adopted(id); }

  /// Read-only view of field id; type_mismatch if T is not its dtype, not_found if absent.
  template <FieldValue T>
  [[nodiscard]] Expected<FieldView<const T>> read(FieldId id) const noexcept {
    if (Status s = check_dtype(id, dtype_of_v<T>); !s.ok()) {
      return make_error(std::move(s));
    }
    Expected<std::span<const std::byte>> bytes = store_.get(id);
    if (!bytes) {
      return make_error(std::move(bytes.error()));
    }
    return FieldView<const T>{typed<const T>(*bytes), grid_};
  }

  /// Writable view of field id, allocated and zero-filled if absent; type_mismatch if T is not
  /// its dtype, out_of_memory if the allocation fails.
  template <FieldValue T>
  [[nodiscard]] Expected<FieldView<T>> write(FieldId id) noexcept {
    if (Status s = check_dtype(id, dtype_of_v<T>); !s.ok()) {
      return make_error(std::move(s));
    }
    Expected<std::span<std::byte>> bytes = store_.ensure(id);
    if (!bytes) {
      return make_error(std::move(bytes.error()));
    }
    return FieldView<T>{typed<T>(*bytes), grid_};
  }

  /// read<field_t<F>>(F): the type follows from the registry at compile time.
  template <FieldId F>
  [[nodiscard]] Expected<FieldView<const field_t<F>>> read() const noexcept {
    return read<field_t<F>>(F);
  }

  /// write<field_t<F>>(F).
  template <FieldId F>
  [[nodiscard]] Expected<FieldView<field_t<F>>> write() noexcept { return write<field_t<F>>(F); }

  /// Uses a caller-owned buffer as field id without copying (plan section 6.6). external must
  /// hold grid().cells() values of the field's dtype and outlive the state or release(id).
  Status adopt(FieldId id, std::span<std::byte> external) noexcept {
    return store_.adopt(id, external);
  }

  /// Typed adopt; type_mismatch if T is not the field's dtype.
  template <FieldValue T>
  Status adopt(FieldId id, std::span<T> external) noexcept {
    if (Status s = check_dtype(id, dtype_of_v<T>); !s.ok()) {
      return s;
    }
    return store_.adopt(id, std::as_writable_bytes(external));
  }

  /// Drops a field no later stage reads: owned memory is freed, adopted memory forgotten.
  void release(FieldId id) noexcept { store_.release(id); }

  [[nodiscard]] VectorLayers& layers() noexcept { return layers_; }
  [[nodiscard]] const VectorLayers& layers() const noexcept { return layers_; }

  [[nodiscard]] Provenance& provenance() noexcept { return provenance_; }
  [[nodiscard]] const Provenance& provenance() const noexcept { return provenance_; }

  /// Bytes of field storage allocated by this state (adopted buffers not included).
  [[nodiscard]] std::size_t bytes_owned() const noexcept { return store_.bytes_owned(); }

 private:
  WorldState(const Grid& grid, MemoryResource& resource) noexcept;

  static Status check_dtype(FieldId id, DType requested) noexcept;

  template <class T, class B>
  static std::span<T> typed(std::span<B> bytes) noexcept {
    return {reinterpret_cast<T*>(bytes.data()), bytes.size() / sizeof(T)};
  }

  Grid grid_;
  FieldStore store_;
  VectorLayers layers_;
  Provenance provenance_;
};

}  // namespace wg::core
