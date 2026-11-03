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
int PersistMinimalApplicationEmbeddedUserOverlay(
    MinimalApplication& application, DirectoryDomainStorage& storage);

// Model-thread checkpoint persist for the live session root in |domain|.
int PersistMinimalApplicationFromDomain(
    ae::Domain& domain, std::filesystem::path const& state_dir);

}  // namespace apptraverse::example::minimal

#endif  // APPTRAVERSE_EXAMPLE_MINIMAL_MINIMAL_APP_PERSISTENCE_H_
