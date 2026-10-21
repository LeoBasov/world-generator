#pragma once

#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

namespace wg::core {

template <class Signature>
class FunctionRef;

/// Non-owning reference to a callable, the body type of Executor::parallel_for (plan section
/// 6.4). Two pointers, trivially copyable, no allocation (C++26 std::function_ref is not yet
/// available). The referenced callable must outlive every call; binding a temporary lambda to a
/// parameter is fine, storing a FunctionRef to one is not. Plain functions bind through a
/// function pointer variable.
template <class R, class... Args>
class FunctionRef<R(Args...)> {
 public:
  // The forwarding constructor is constrained to exclude FunctionRef, so it never hides the copy
  // constructor. The callable may itself be a pointer (a function pointer variable); its address
  // is erased on purpose and cast back to the same type in invoke.
  // NOLINTBEGIN(bugprone-forwarding-reference-overload)
  // NOLINTBEGIN(bugprone-multi-level-implicit-pointer-conversion)
  template <class F>
    requires(!std::is_same_v<std::remove_cvref_t<F>, FunctionRef> &&
             !std::is_function_v<std::remove_reference_t<F>> &&
             std::is_invocable_r_v<R, std::remove_reference_t<F>&, Args...>)
  FunctionRef(F&& f) noexcept
      : object_(const_cast<void*>(static_cast<const void*>(std::addressof(f)))),
        call_(&invoke<std::remove_reference_t<F>>) {}
  // NOLINTEND(bugprone-multi-level-implicit-pointer-conversion)
  // NOLINTEND(bugprone-forwarding-reference-overload)

  R operator()(Args... args) const { return call_(object_, std::forward<Args>(args)...); }

 private:
  template <class T>
  static R invoke(void* object, Args... args) {
    if constexpr (std::is_void_v<R>) {
      std::invoke(*static_cast<T*>(object), std::forward<Args>(args)...);
    } else {
      return std::invoke(*static_cast<T*>(object), std::forward<Args>(args)...);
    }
  }

  void* object_;
  R (*call_)(void*, Args...);
};

}  // namespace wg::core
