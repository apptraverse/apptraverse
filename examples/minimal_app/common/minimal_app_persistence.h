#ifndef APPTRAVERSE_EXAMPLE_MINIMAL_MINIMAL_APP_PERSISTENCE_H_
#define APPTRAVERSE_EXAMPLE_MINIMAL_MINIMAL_APP_PERSISTENCE_H_

#include <filesystem>
#include <functional>

#include "aether-objects/obj/domain.h"

#include "minimal_application.h"

namespace apptraverse {
class DirectoryDomainStorage;
}

namespace apptraverse::example::minimal {

using MinimalApplicationGraphBuilder =
    std::function<MinimalApplication::ptr(ae::Domain& domain)>;

// Returns true when a distilled bootstrap graph was written (empty/absent profile).
bool BootstrapMinimalApplicationProfileIfAbsent(
    std::filesystem::path const& state_dir,
    MinimalApplicationGraphBuilder const& build_graph);

int PersistMinimalApplication(MinimalApplication& application,
                              DirectoryDomainStorage& storage);

// USER overlay persist for build-time embedded BACKING sessions (no full-graph SaveRoot).
int SaveMinimalApplicationEmbeddedUserState(MinimalApplication& application);

int PersistMinimalApplicationEmbeddedUserOverlay(
    MinimalApplication& application, DirectoryDomainStorage& storage);

// Journal-bearing nodes: adopt generation/base before embedded USER save.
void FinalizeMinimalApplicationNodesForEmbeddedUserOverlay(
    MinimalApplication& application);

// After embedded USER reload: restore Node base bindings before UI commands.
void NormalizeMinimalApplicationAfterEmbeddedUserLoad(
    MinimalApplication& application);

// Model-thread checkpoint of the live embedded session root in |domain|; the
// session writes through its overlay into |user_storage|.
int PersistMinimalApplicationEmbeddedUserOverlayFromDomain(
    ae::Domain& domain, DirectoryDomainStorage& user_storage);

}  // namespace apptraverse::example::minimal

#endif  // APPTRAVERSE_EXAMPLE_MINIMAL_MINIMAL_APP_PERSISTENCE_H_
