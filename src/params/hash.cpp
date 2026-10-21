#include "wg/params/hash.hpp"

#include <cstddef>
#include <cstdint>

#include "encoder.hpp"

namespace wg::params {

std::uint64_t params_hash(const GenParams& params) noexcept {
  detail::Encoder e;
  e.text("wg.params");
  e.i64(kParamsSchemaVersion);
  for (const ParamDescriptor& d : kParamDescriptors) {
    e.text(d.path);
    e.u8(static_cast<std::uint8_t>(d.kind));
    e.u64(d.count);
    const std::size_t n = d.count == 0 ? 1 : d.count;
    for (std::size_t i = 0; i < n; ++i) {
      const double v = get_param(params, d, i);
      switch (d.kind) {
        case ParamKind::real:
          e.real(v);
          break;
        case ParamKind::integer:
          e.i64(static_cast<std::int64_t>(v));
          break;
        case ParamKind::boolean:
        case ParamKind::enumeration:
          e.u8(static_cast<std::uint8_t>(v));
          break;
      }
    }
  }
  return e.hash();
}

}  // namespace wg::params
