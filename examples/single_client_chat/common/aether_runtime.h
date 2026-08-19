#ifndef APPTRAVERSE_EXAMPLES_AETHER_RUNTIME_H_
#define APPTRAVERSE_EXAMPLES_AETHER_RUNTIME_H_

#include <cassert>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "aether/all.h"

#if defined(__ANDROID__)
#  include "android_system_dns_resolver.h"
#endif

namespace apptraverse::examples {

// Official Aether example parent UID (registration cloud).
inline constexpr auto kAetherParentUid =
    ae::Uid::FromString("3ac93165-3d37-4970-87a6-fa4ee27744e4");

inline constexpr char kWindowsAetherClientName[] = "apptraverse-windows";
inline constexpr char kAndroidAetherClientName[] = "apptraverse-android";

// AetherApp owns storage; keep a non-owning pointer for SyncReplica.
struct ConstructedAetherRuntime {
  std::unique_ptr<ae::AetherApp> app;
  ae::IDomainStorage* storage{nullptr};
};

// One AetherApp + Domain + storage, with EthernetAdapter when distilling.
// Calls storage_factory once, keeps a non-owning pointer, then transfers that
// same unique_ptr into AetherApp (no second storage instance).
template <typename StorageFactory>
ConstructedAetherRuntime ConstructAetherAppWithEthernet(
    StorageFactory&& storage_factory) {
  ConstructedAetherRuntime out;
  auto held = std::make_shared<std::unique_ptr<ae::IDomainStorage>>(
      std::forward<StorageFactory>(storage_factory)());
  out.storage = held->get();
  assert(out.storage != nullptr);

  // SmallFunction storage is tiny; capture only the shared_ptr and move once.
  auto transfer = [held]() {
    assert(held != nullptr && held->get() != nullptr);
    return std::move(*held);
  };

#if AE_DISTILLATION && defined(__ANDROID__)
  out.app = ae::AetherApp::Construct(
      ae::AetherAppContext{std::move(transfer)}
          .AddAdapterFactory([](ae::AetherAppContext const& ctx) {
            return ae::EthernetAdapter::ptr::Create(
                ae::CreateWith{ctx.domain()}.with_id(
                    ae::GlobalId::kEthernetAdapter),
                ctx.aether(), ctx.poller(), ctx.dns_resolver());
          })
          .DnsResolverFactory([](ae::AetherAppContext const& ctx) {
            return AndroidSystemDnsResolver::ptr::Create(
                ae::CreateWith{ctx.domain()}
                    .with_id(ae::GlobalId::kDnsResolver)
                    .with_flags(ae::ObjFlags::kUnloadedByDefault),
                ctx.aether());
          }));
#elif AE_DISTILLATION
  out.app = ae::AetherApp::Construct(
      ae::AetherAppContext{std::move(transfer)}
          .AddAdapterFactory([](ae::AetherAppContext const& ctx) {
            return ae::EthernetAdapter::ptr::Create(
                ae::CreateWith{ctx.domain()}.with_id(
                    ae::GlobalId::kEthernetAdapter),
                ctx.aether(), ctx.poller(), ctx.dns_resolver());
          }));
#else
  out.app = ae::AetherApp::Construct(
      ae::AetherAppContext{std::move(transfer)});
#endif
  assert(out.storage != nullptr);
  return out;
}

inline std::string FormatAetherUid(ae::Uid const& uid) {
  return ae::Format("{}", uid);
}

// Blocks on the AetherApp event loop until SelectClient finishes.
// Returns an empty Client::ptr on failure. When out_error is set, writes the
// SelectClient error code (or -1 if the action finished without a result).
inline ae::Client::ptr SelectPersistentAetherClient(
    ae::AetherApp& aether_app, std::string_view client_name,
    int* out_error = nullptr) {
  ae::Client::ptr client;
  int error_code = 0;
  bool got_result = false;
  auto& action = aether_app.aether()->SelectClient(
      kAetherParentUid, std::string{client_name});
  action.result_event().Subscribe([&](auto const& result) {
    got_result = true;
    if (result) {
      client = result.value();
      error_code = 0;
    } else {
      error_code = result.error();
    }
  });
  aether_app.WaitActions(action);
  if (!client) {
    if (out_error != nullptr) {
      *out_error = got_result ? error_code : -1;
    }
    return client;
  }
  // Persist clients_ immediately. Force-stop / kill skips ~AetherApp, which
  // would otherwise be the only place that saves the updated Aether object.
  aether_app.aether().Save();
  return client;
}

}  // namespace apptraverse::examples

#endif  // APPTRAVERSE_EXAMPLES_AETHER_RUNTIME_H_
