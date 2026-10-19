#include "apptraverse/model_object_proxy.h"

#include "apptraverse/surfaces/surfaces_presentation_size_queue.h"

namespace apptraverse {

void ModelObjectProxy::BindPresentationSizeQueue(
    SurfacesPresentationSizeQueue* queue) {
  assert(queue != nullptr);
  presentation_size_queue_ = queue;
}

void ModelObjectProxy::UnbindPresentationSizeQueue(
    SurfacesPresentationSizeQueue* queue) {
  if (presentation_size_queue_ == queue) {
    presentation_size_queue_ = nullptr;
  }
}

}  // namespace apptraverse
