#ifndef APPTRAVERSE_EMBEDDED_APPLICATION_MODEL_SESSION_H_
#define APPTRAVERSE_EMBEDDED_APPLICATION_MODEL_SESSION_H_

#include <utility>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/idomain_storage.h"
#include "aether-objects/obj/obj_id.h"

#include "apptraverse/application_model_session.h"
#include "apptraverse/client_local_state.h"
#include "apptraverse/embedded_distilled_domain.h"
#include "apptraverse/overlay_domain_storage.h"

namespace apptraverse {

template <typename TApplication>
int RunClientLocalApplicationModelSession(
    ae::RamDomainStorage& user_storage, ae::RamDomainStorage& local_storage,
    ae::RamDomainStorage& backing, std::set<ae::ObjId> const& constants,
    ae::ObjId root_id, ApplicationModelRuntime& runtime,
    ApplicationPublishedCallback on_published,
    ApplicationModelSessionHooks<TApplication> const& hooks) {
  ClientLocalDomainStorage routed{user_storage, local_storage, backing,
                                  CollectClientLocalRoots(backing)};
  OverlayDomainStorage overlay{routed, backing, constants};
  return RunApplicationModelSession(overlay, root_id, runtime,
      std::move(on_published), hooks);
}

// Route supply-marked local ownership graphs before loading the application.
// RunApplicationModelSession invokes prepare before its initial publication.
template <typename TApplication>
int RunEmbeddedApplicationModelSession(
    ae::RamDomainStorage& user_storage, ae::RamDomainStorage& local_storage,
    EmbeddedDistilledGraph const& graph, ApplicationModelRuntime& runtime,
    ApplicationPublishedCallback on_published,
    ApplicationModelSessionHooks<TApplication> const& hooks) {
  ae::RamDomainStorage backing;
  InstallEmbeddedDistilledBacking(backing, graph);
  return RunClientLocalApplicationModelSession(user_storage, local_storage, backing,
      EmbeddedSupplyConstantIdSet(graph), ae::ObjId{graph.root_object_id},
      runtime, std::move(on_published), hooks);
}

template <typename TApplication>
int RunEmbeddedApplicationModelSession(
    ae::IDomainStorage& user_storage, EmbeddedDistilledGraph const& graph,
    ApplicationModelRuntime& runtime,
    ApplicationPublishedCallback on_published,
    ApplicationModelSessionHooks<TApplication> const& hooks) {
  ae::RamDomainStorage backing;
  InstallEmbeddedDistilledBacking(backing, graph);
  OverlayDomainStorage overlay{
      user_storage, backing, EmbeddedSupplyConstantIdSet(graph)};
  ae::ObjId const root_id{graph.root_object_id};
  return RunApplicationModelSession(overlay, root_id, runtime,
                                    std::move(on_published), hooks);
}

}  // namespace apptraverse

#endif  // APPTRAVERSE_EMBEDDED_APPLICATION_MODEL_SESSION_H_
