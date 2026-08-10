#ifndef APPTRAVERSE_EXAMPLES_AETHER_RUNTIME_H_
#define APPTRAVERSE_EXAMPLES_AETHER_RUNTIME_H_

#include <string>
#include <string_view>
#include <utility>

#include "aether/all.h"

namespace apptraverse::examples {

// Official Aether example parent UID (registration cloud).
inline constexpr auto kAetherParentUid =
    ae::Uid::FromString("3ac93165-3d37-4970-87a6-fa4ee27744e4");

inline constexpr char kWindowsAetherClientName[] = "apptraverse-windows";
inline constexpr char kAndroidAetherClientName[] = "apptraverse-android";

// One AetherApp + Domain + storage, with EthernetAdapter when distilling.
template <typename StorageFactory>
ae::RcPtr<ae::AetherApp> ConstructAetherAppWithEthernet(
    StorageFactory&& storage_factory) {
  return ae::AetherApp::Construct(
      ae::AetherAppContext{std::forward<StorageFactory>(storage_factory)}
#if AE_DISTILLATION
          .AddAdapterFactory([](ae::AetherAppContext const& context) {
            return ae::EthernetAdapter::ptr::Create(
                ae::CreateWith{context.domain()}.with_id(
                    ae::GlobalId::kEthernetAdapter),
                context.aether(), context.poller(), context.dns_resolver());
          })
#endif
  );
}

inline std::string FormatAetherUid(ae::Uid const& uid) {
  return ae::Format("{}", uid);
}

// Blocks on the AetherApp event loop until SelectClient finishes.
// Returns an empty Client::ptr on failure.
inline ae::Client::ptr SelectPersistentAetherClient(
    ae::RcPtr<ae::AetherApp> const& aether_app, std::string_view client_name) {
  ae::Client::ptr client;
  auto& action = aether_app->aether()->SelectClient(
      kAetherParentUid, std::string{client_name});
  action.result_event().Subscribe([&](auto const& result) {
    if (result) {
      client = result.value();
    }
  });
  aether_app->WaitActions(action);
  return client;
}

}  // namespace apptraverse::examples

#endif  // APPTRAVERSE_EXAMPLES_AETHER_RUNTIME_H_
