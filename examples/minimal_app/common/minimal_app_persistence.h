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

}  // namespace apptraverse::example::minimal

#endif  // APPTRAVERSE_EXAMPLE_MINIMAL_MINIMAL_APP_PERSISTENCE_H_
