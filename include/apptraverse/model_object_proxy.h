#ifndef APPTRAVERSE_MODEL_OBJECT_PROXY_H_
#define APPTRAVERSE_MODEL_OBJECT_PROXY_H_

#include <cassert>
#include <functional>
#include <utility>

#include "aether-objects/obj/domain.h"
#include "aether-objects/obj/obj_id.h"

namespace apptraverse {

// Cross-domain GUI → model invoke: only ObjId + member-function identity cross
// the thread boundary. The queued work resolves the object on the model Domain.
class ModelObjectProxy {
 public:
  using ModelWork = std::function<void(ae::Domain&)>;
  using Enqueue = std::function<void(ModelWork)>;

  explicit ModelObjectProxy(Enqueue enqueue) : enqueue_{std::move(enqueue)} {
    assert(enqueue_);
  }

  template <typename T>
  void Invoke(ae::ObjId id, void (T::*method)()) {
    assert(method != nullptr);
    enqueue_([id, method](ae::Domain& domain) {
      auto object = domain.Find(id);
      assert(object && "model proxy target must exist");
      auto* target = dynamic_cast<T*>(&*object);
      assert(target && "model proxy target type mismatch");
      (target->*method)();
    });
  }

 private:
  Enqueue enqueue_;
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_MODEL_OBJECT_PROXY_H_
