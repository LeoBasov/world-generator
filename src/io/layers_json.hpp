#pragma once

// Internal JSON form of the vector layers (not installed), shared by the metadata chunk of the
// world file and the JSON export (A09). Every struct of core/vector_layers.hpp is an object with
// its member names as keys, the river graph keeps its struct-of-arrays form, and enumerations
// are labels. Floats are written as the shortest double that reads back to the same value, so
// decode(encode(layers)) == layers bit for bit.

#include <string_view>

#include "io/json.hpp"
#include "wg/core/expected.hpp"
#include "wg/core/vector_layers.hpp"

namespace wg::io::detail {

/// JSON object of all layers. Errors: invalid_argument for a non-finite number (JSON has none)
/// or a river graph whose arrays differ in length, naming the layer and index.
[[nodiscard]] core::Expected<Json> encode_layers(const core::VectorLayers& layers) noexcept;

/// Layers from the object written by encode_layers. Errors: parse_error whose message lists
/// one "<path>: <message>" line per problem (missing or unknown keys, wrong types, values out
/// of range of their C++ type, unknown labels, river arrays of different lengths).
[[nodiscard]] core::Expected<core::VectorLayers> decode_layers(const Json& json,
                                                               std::string_view path) noexcept;

}  // namespace wg::io::detail
