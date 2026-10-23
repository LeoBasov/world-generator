// C1 Allocate and orient (S1, A12). The runner allocates the WorldState on the parameters' grid;
// this stage orients it: latitude from the pole option (R5.3, ADR 0010).

#include <memory>
#include <new>
#include <string_view>
#include <utility>

#include "stages/classic/classic_stages.hpp"
#include "wg/climate/latitude.hpp"

namespace wg::stages {

namespace {

using core::FieldId;
using pipeline::FieldSet;
using pipeline::StageContext;
using pipeline::StageResult;

class C01Allocate final : public pipeline::Stage {
 public:
  [[nodiscard]] std::string_view id() const noexcept override { return kC01Allocate; }
  [[nodiscard]] FieldSet inputs() const noexcept override { return {}; }
  [[nodiscard]] FieldSet outputs() const noexcept override { return {FieldId::latitude}; }

  StageResult run(StageContext& ctx) noexcept override {
    try {
      return run_checked(ctx);
    } catch (...) {
      return StageResult::error(
          core::make_status(core::StatusCode::out_of_memory, "c01_allocate: out of memory"));
    }
  }

 private:
  static StageResult run_checked(StageContext& ctx) {
    core::Expected<core::FieldView<float>> phi = ctx.state.write<FieldId::latitude>();
    if (!phi) {
      return failed(std::move(phi.error()));
    }
    core::Status status = climate::fill_latitude(ctx.params.poles, ctx.state.grid(), phi->data,
                                                 ctx.exec, &ctx.cancel);
    if (!status.ok()) {
      return failed(std::move(status));
    }
    StageResult result = StageResult::ok();
    result.metrics.add("cells", static_cast<double>(ctx.state.grid().cells()));
    return result;
  }
};

}  // namespace

std::unique_ptr<pipeline::Stage> make_c01_allocate() noexcept {
  return std::unique_ptr<pipeline::Stage>(new (std::nothrow) C01Allocate());
}

}  // namespace wg::stages
