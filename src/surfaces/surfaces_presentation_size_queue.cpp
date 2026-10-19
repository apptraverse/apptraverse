#include "apptraverse/surfaces/surfaces_presentation_size_queue.h"

#include <cassert>

#include "apptraverse/surfaces/surfaces_model.h"

namespace apptraverse {

void PostSurfacePresentationSize(ModelObjectProxy* proxy, ae::ObjId surface_id,
                                 std::int32_t width, std::int32_t height) {
  assert(proxy != nullptr);
  if (PresentationSizeQueue* queue = proxy->GetPresentationSizeQueue()) {
    queue->Post(proxy, surface_id, width, height);
    return;
  }
  proxy->Invoke<Surface>(surface_id, &Surface::SetPresentationSize, width,
                         height);
}

}  // namespace apptraverse
