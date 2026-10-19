#include "apptraverse/model_object_proxy.h"

#include "apptraverse/presentation_size_queue.h"

namespace apptraverse {

void ModelObjectProxy::BindPresentationSizeQueue(
    PresentationSizeQueue* queue) {
  assert(queue != nullptr);
  presentation_size_queue_ = queue;
}

void ModelObjectProxy::UnbindPresentationSizeQueue(
    PresentationSizeQueue* queue) {
  if (presentation_size_queue_ == queue) {
    presentation_size_queue_ = nullptr;
  }
}

}  // namespace apptraverse
