#ifndef APPTRAVERSE_SURFACES_LIFECYCLE_H_
#define APPTRAVERSE_SURFACES_LIFECYCLE_H_

#include "apptraverse/application/model_publication_session.h"

#include "apptraverse/surfaces/surfaces_model.h"

#include "surfaces_runtime_execution.h"

namespace apptraverse {

using SurfacesPublicationKind = ModelPublicationKind;
using SurfacesModelSession = ModelPublicationSession;

void ConfigureSurfacesDemoModelSession(SurfacesModelSession& session);

void RunSurfacesDemoModelSession(
    SurfacesModelSession& session,
    std::function<void(SurfacesPublicationKind)> on_published);

void ApplySurfacesStructural(std::vector<std::uint8_t> const& bytes,
                             Application& ui_application,
                             ae::IDomainStorage& ui_storage, void* host,
                             ModelObjectProxy* model_proxy = nullptr);

}  // namespace apptraverse

#endif  // APPTRAVERSE_SURFACES_LIFECYCLE_H_
