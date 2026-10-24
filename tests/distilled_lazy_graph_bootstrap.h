#ifndef APPTRAVERSE_TESTS_DISTILLED_LAZY_GRAPH_BOOTSTRAP_H_
#define APPTRAVERSE_TESTS_DISTILLED_LAZY_GRAPH_BOOTSTRAP_H_

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/obj_id.h"

#include "apptraverse/embedded_distilled_domain.h"

namespace apptraverse::test {

ae::ObjId DistillLazyDocGraphIntoStorage(
    ae::RamDomainStorage& storage,
    ae::RamDomainStorage* export_storage = nullptr);

void EnsureLazyDistilledGraphRegistration();

void RunEmbeddedLazyDistilledConsumerChecks(
    EmbeddedDistilledGraph const& graph);

}  // namespace apptraverse::test

#endif  // APPTRAVERSE_TESTS_DISTILLED_LAZY_GRAPH_BOOTSTRAP_H_
