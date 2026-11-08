#ifndef APPTRAVERSE_EMBEDDED_APPLICATION_MODEL_SESSION_H_
#define APPTRAVERSE_EMBEDDED_APPLICATION_MODEL_SESSION_H_

#include <utility>

#include "aether-objects/obj/obj_id.h"

#include "apptraverse/application_model_session.h"
#include "apptraverse/application_model_session_storage.h"
#include "apptraverse/embedded_distilled_domain.h"

namespace apptraverse {

template <typename TApplication>
int RunEmbeddedApplicationModelSession(
    ApplicationModelSessionStorage& session, ApplicationModelRuntime& runtime,
    ApplicationPublishedCallback on_published,
    ApplicationModelSessionHooks<TApplication> const& hooks) {
  ApplicationModelSessionStorage::ActivateScope const scope{session};
  ae::ObjId const root_id{session.root_id()};
  return RunApplicationModelSession(session.model_overlay(), root_id, runtime,
                                    std::move(on_published), hooks);
}

template <typename TApplication>
int RunEmbeddedApplicationModelSession(
    DirectoryDomainStorage& user_storage, EmbeddedDistilledGraph const& graph,
    ApplicationModelRuntime& runtime,
    ApplicationPublishedCallback on_published,
    ApplicationModelSessionHooks<TApplication> const& hooks) {
  ApplicationModelSessionStorage session{user_storage, graph};
  return RunEmbeddedApplicationModelSession(session, runtime,
                                            std::move(on_published), hooks);
}

}  // namespace apptraverse

#endif  // APPTRAVERSE_EMBEDDED_APPLICATION_MODEL_SESSION_H_
