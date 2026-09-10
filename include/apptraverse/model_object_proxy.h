#ifndef APPTRAVERSE_MODEL_OBJECT_PROXY_H_
#define APPTRAVERSE_MODEL_OBJECT_PROXY_H_

#include <cassert>
#include <functional>
#include <utility>

#include "aether-objects/obj/domain.h"
#include "aether-objects/obj/obj_id.h"

namespace apptraverse {

// Cross-domain GUI → model invoke: only ObjId + member-function identity and
// by-value arguments cross the thread boundary. The queued work resolves the
// object on the model Domain.
class ModelObjectProxy {
 public:
  using ModelWork = std::function<void(ae::Domain&)>;
  using Enqueue = std::function<void(ModelWork)>;

  explicit ModelObjectProxy(Enqueue enqueue) : enqueue_{std::move(enqueue)} {
    assert(enqueue_);
  }

  // Args are captured by value so they outlive the GUI call until model
  // execution. Zero-argument methods use an empty pack.
  template <typename T, typename... Args>
  void Invoke(ae::ObjId id, void (T::*method)(Args...), Args... args) {
    assert(method != nullptr);
    enqueue_([id, method, args...](ae::Domain& domain) {
      auto object = domain.Find(id);
      assert(object && "model proxy target must exist");
      // Architecture selects T; aether Ptr::as is the typed conversion API
      // (unchecked static_cast — no C++ RTTI).
      T* const target = object.as<T>();
      (target->*method)(args...);
    });
  }

 private:
  Enqueue enqueue_;
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_MODEL_OBJECT_PROXY_H_
