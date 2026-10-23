#include "field_values.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <string>

#include "wg/core/dtype.hpp"

namespace wg::cli {

namespace {

template <class T>
void convert(const std::vector<std::byte>& bytes, std::vector<double>& out) {
  const std::size_t n = bytes.size() / sizeof(T);
  out.resize(n);
  for (std::size_t i = 0; i < n; ++i) {
    T v{};
    std::memcpy(&v, bytes.data() + (i * sizeof(T)), sizeof(T));
    out[i] = static_cast<double>(v);
  }
}

}  // namespace

core::Expected<std::vector<core::FieldId>> select_fields(const std::vector<std::string>& names,
                                                         const io::WorldReader& reader) {
  std::vector<core::FieldId> ids;
  const bool all = names.empty() || std::find(names.begin(), names.end(), "all") != names.end();
  if (all) {
    for (std::size_t i = 0; i < core::kFieldCount; ++i) {
      const auto id = static_cast<core::FieldId>(i);
      if (reader.has(id)) {
        ids.push_back(id);
      }
    }
    return ids;
  }
  for (const std::string& name : names) {
    const std::optional<core::FieldId> id = core::field_id_from_name(name);
    if (!id) {
      return core::make_error(core::StatusCode::invalid_argument, "unknown field '" + name + "'");
    }
    if (!reader.has(*id)) {
      return core::make_error(core::StatusCode::not_found, "the world has no field '" + name + "'");
    }
    if (std::find(ids.begin(), ids.end(), *id) == ids.end()) {
      ids.push_back(*id);
    }
  }
  return ids;
}

core::Expected<std::vector<double>> read_values(io::WorldReader& reader, core::FieldId id) {
  const io::ChunkEntry* chunk = reader.chunk(id);
  if (chunk == nullptr) {
    return core::make_error(
        core::StatusCode::not_found,
        "the world has no field '" + std::string(core::field_info(id).name) + "'");
  }
  std::vector<std::byte> bytes(static_cast<std::size_t>(chunk->raw_size));
  if (core::Status s = reader.read_field(id, bytes); !s.ok()) {
    return core::make_error(std::move(s));
  }
  std::vector<double> values;
  switch (core::field_info(id).dtype) {
    case core::DType::u8:
      convert<std::uint8_t>(bytes, values);
      break;
    case core::DType::u16:
      convert<std::uint16_t>(bytes, values);
      break;
    case core::DType::i32:
      convert<std::int32_t>(bytes, values);
      break;
    case core::DType::f32:
      convert<float>(bytes, values);
      break;
  }
  return values;
}

FieldStats field_stats(const std::vector<double>& values) noexcept {
  FieldStats s;
  double sum = 0.0;
  for (const double v : values) {
    if (!std::isfinite(v)) {
      ++s.non_finite;
      continue;
    }
    if (s.finite == 0) {
      s.min = v;
      s.max = v;
    }
    s.min = std::min(s.min, v);
    s.max = std::max(s.max, v);
    sum += v;
    ++s.finite;
  }
  s.mean = s.finite > 0 ? sum / static_cast<double>(s.finite) : 0.0;
  return s;
}

}  // namespace wg::cli
