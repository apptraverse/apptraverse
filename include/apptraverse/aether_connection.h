#pragma once
#include <filesystem>

#include "apptraverse/connection_runtime.h"
namespace apptraverse {
// A worker-affine SDK identity/connection, shared by data and/or presence
// adapters. A presence-only connection can accompany an unrelated router link.
class AetherConnection;
std::shared_ptr<AetherConnection> OpenAetherConnection(
    std::filesystem::path profile, std::string client_name);
std::unique_ptr<IConnectionTransport> MakeAetherTransport(
    std::shared_ptr<AetherConnection>);
// Infrastructure services use the original link to avoid recursive mailboxes.
std::unique_ptr<IConnectionTransport> MakeDirectAetherTransport(
    std::shared_ptr<AetherConnection>);
std::unique_ptr<IPresenceSource> MakeAetherPresence(
    std::shared_ptr<AetherConnection>);
ConnectionAdapters MakeAetherConnectionAdapters(std::filesystem::path profile,
                                                std::string client_name);
}  // namespace apptraverse
