#ifndef APPTRAVERSE_EMBEDDED_APPLICATION_MODEL_SESSION_H_
#define APPTRAVERSE_EMBEDDED_APPLICATION_MODEL_SESSION_H_

#include <utility>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/obj_id.h"

#include "apptraverse/application_model_session.h"
#include "apptraverse/embedded_distilled_domain.h"
#include "apptraverse/overlay_domain_storage.h"

namespace apptraverse {

template <typename TApplication>
int RunEmbeddedApplicationModelSession(
    ae::IDomainStorage& user_storage, EmbeddedDistilledGraph const& graph,
    ApplicationModelRuntime& runtime,
    ApplicationPublishedCallback on_published,
    ApplicationModelSessionHooks<TApplication> const& hooks) {
  ae::RamDomainStorage backing;
  InstallEmbeddedDistilledBacking(backing, graph);
  OverlayDomainStorage overlay{user_storage, backing,
                               EmbeddedSupplyConstantIdSet(graph)};
  ae::ObjId const root_id{graph.root_object_id};
  return RunApplicationModelSession(overlay, root_id, runtime,
                                    std::move(on_published), hooks);
}

}  // namespace apptraverse

#endif  // APPTRAVERSE_EMBEDDED_APPLICATION_MODEL_SESSION_H_
