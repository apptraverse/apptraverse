#include <algorithm>
#include <charconv>
#include <chrono>
#include <csignal>
#include <iostream>
#include <string_view>

#include "aether-miscpp/format/format.h"
#include "aether/ae_actions/select_client.h"
#include "aether/aether_app.h"
#include "aether/global_ids.h"
#include "aether/peer/rendezvous_service.h"
#include "apptraverse/directory_domain_storage.h"

#if !AE_SUPPORT_P2P || !AE_SUPPORT_HOLE_PUNCH
#error The rendezvous service requires the Æther rendezvous user config.
#endif

namespace {
volatile std::sig_atomic_t stopping{};

std::optional<ae::Address> ParsePublicIpv4(std::string_view value) {
  ae::IpV4Addr ip{};
  for (std::size_t i = 0; i < 4; ++i) {
    auto const separator = value.find('.');
    if ((i != 3) != (separator != std::string_view::npos)) return {};
    auto const octet = value.substr(0, separator);
    unsigned number{};
    auto const result =
        std::from_chars(octet.data(), octet.data() + octet.size(), number);
    if (result.ec != std::errc{} || result.ptr != octet.data() + octet.size() ||
        number > 255) return {};
    ip.ipv4_value[i] = static_cast<std::uint8_t>(number);
    if (i != 3) value.remove_prefix(separator + 1);
  }
  if (ip.ipv4_value[0] == 0 || ip.ipv4_value[0] >= 224) return {};
  return ae::Address{ip};
}
}

int main(int argc, char** argv) {
  if (argc != 4) {
    std::cerr << "Usage: shared_chat_rendezvous PROFILE UDP_PORT ADVERTISED_IPV4\n";
    return 2;
  }
  std::string_view port_argument{argv[2]};
  unsigned port{};
  auto const parsed = std::from_chars(port_argument.data(),
                                      port_argument.data() + port_argument.size(), port);
  if (parsed.ec != std::errc{} || parsed.ptr != port_argument.data() + port_argument.size() ||
      port == 0 || port > 65535) return 2;
  auto public_address = ParsePublicIpv4(argv[3]);
  if (!public_address) return 2;
  std::signal(SIGINT, [](int) { stopping = 1; });
  std::signal(SIGTERM, [](int) { stopping = 1; });
  auto const profile = std::filesystem::path{argv[1]};
  auto app = ae::AetherApp::Construct(ae::AetherAppContext{
      [&profile]() -> std::unique_ptr<ae::IDomainStorage> {
        return std::make_unique<apptraverse::DirectoryDomainStorage>(
            profile, apptraverse::DirectoryDomainStorage::ObjectBudget{
                         ae::GlobalId::kTeleStatistics,
                         APPTRAVERSE_AETHER_TELEMETRY_DISK_BYTES});
      }});
  if (!app) return 1;
  ae::Client::ptr client;
  bool failed = false;
  auto selected = app->aether()
      ->SelectClient(ae::Uid::FromString("3ac93165-3d37-4970-87a6-fa4ee27744e4"),
                     "shared-chat-rendezvous")
      .result_event().Subscribe([&](auto const& result) {
        if (result) client = result.value();
        else failed = true;
      });
  auto const deadline = ae::Now() + std::chrono::seconds{90};
  auto update = [&] {
    auto const wakeup = app->Update(ae::Now());
    app->WaitUntil(std::min(wakeup, ae::Now() + std::chrono::milliseconds{20}));
  };
  while (!stopping && !failed && !client.is_valid() && ae::Now() < deadline) update();
  if (!client.is_valid() || stopping) return 1;
  ae::RendezvousService service{
      ae::AeContext{*client}, client.Load(),
      ae::Address{ae::IpV4Addr{{0, 0, 0, 0}}}, static_cast<std::uint16_t>(port),
      std::move(*public_address)};
  if (service.port() != port) return 1;
  client.Save();
  app->aether().Save();
  std::cout << ae::Format("RENDEZVOUS_READY uid={} endpoint={}:{}", client->uid(),
                         std::string_view{argv[3]}, port)
            << std::endl;
  auto registered_sockets = service.registered_sockets();
  while (!stopping && !app->IsExited()) {
    update();
    auto const current = service.registered_sockets();
    if (current != registered_sockets) {
      registered_sockets = current;
      std::cout << "RENDEZVOUS_SOCKETS " << registered_sockets << std::endl;
    }
  }
  return 0;
}
