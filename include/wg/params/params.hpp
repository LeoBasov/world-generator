#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "wg/generated/gen_params.hpp"

namespace wg::params {

// The parameter model is generated from schema/params.schema.json (A07): GenParams{} holds the
// schema defaults, and kParamDescriptors lists every parameter with its JSON path, bounds and
// DF name in schema order. The functions below access a parameter through its descriptor.

/// A params.json file: the parameters plus the run defaults stored next to them. Seed and
/// profile are not part of the parameter hash; wgcli options override them.
struct ParamsDocument {
  GenParams params;
  std::optional<std::uint64_t> seed;
  std::optional<std::string> profile;

  friend bool operator==(const ParamsDocument&, const ParamsDocument&) = default;
};

/// Element of a parameter as a double: integers exactly, booleans as 0 or 1, enumerations as
/// the label index. element must be below max(d.count, 1).
[[nodiscard]] double get_param(const GenParams& params, const ParamDescriptor& d,
                               std::size_t element = 0) noexcept;

/// Sets an element of a parameter from a double: integers are truncated toward zero, booleans
/// are value != 0, enumerations take the label index. Bounds are not checked (see validate).
/// element must be below max(d.count, 1).
void set_param(GenParams& params, const ParamDescriptor& d, std::size_t element,
               double value) noexcept;

/// Label of an enumeration parameter, empty for other kinds or an index outside the labels.
[[nodiscard]] std::string_view param_label(const GenParams& params,
                                           const ParamDescriptor& d) noexcept;

}  // namespace wg::params
