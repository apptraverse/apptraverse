#ifndef APPTRAVERSE_SURFACES_PRESENTATION_SIZE_QUEUE_H_
#define APPTRAVERSE_SURFACES_PRESENTATION_SIZE_QUEUE_H_

#include "apptraverse/presentation_size_queue.h"

namespace apptraverse {

using SurfacesPresentationSizeQueue = PresentationSizeQueue;

void PostSurfacePresentationSize(ModelObjectProxy* proxy, ae::ObjId surface_id,
                                   std::int32_t width, std::int32_t height);

}  // namespace apptraverse

#endif  // APPTRAVERSE_SURFACES_PRESENTATION_SIZE_QUEUE_H_
