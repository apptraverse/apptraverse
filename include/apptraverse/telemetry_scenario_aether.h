#pragma once
// Supported offline SDK context: native object/runtime factories, no client,
// socket, registration, DNS or fabricated transport/presence observations.
#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether/aether_app.h"
#include "aether/global_ids.h"
#include "apptraverse/aether_telemetry_delivery.h"
#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/telemetry_scenario.h"
namespace apptraverse {
inline std::unique_ptr<ae::AetherApp> MakeOfflineTelemetryApp(
    std::filesystem::path profile = {}, bool owner = true) {
  auto profile_holder =
      std::make_shared<std::filesystem::path>(std::move(profile));
  auto context = ae::AetherAppContext{
      [profile_holder]() -> std::unique_ptr<ae::IDomainStorage> {
        if (profile_holder->empty())
          return std::make_unique<ae::RamDomainStorage>();
        return std::make_unique<DirectoryDomainStorage>(*profile_holder);
      }};
  auto owned = std::move(context).TelemetryOwner(owner);
  auto configured =
      std::move(owned)
          .AdaptersFactory([](ae::AetherAppContext const& ctx) {
            auto existing =
                ae::AdapterRegistry::ptr{ctx.aether()->adapter_registry};
            if (existing.is_valid()) return existing;
            return ae::AdapterRegistry::ptr::Create(
                ae::CreateWith{ctx.domain()}.with_id(
                    ae::GlobalId::kAdapterRegistry));
          })
          .CryptoFactory([](auto const&) { return ae::Crypto::ptr{}; })
          .PollerFactory([](auto const&) { return ae::IPoller::ptr{}; });
#if AE_SUPPORT_REGISTRATION
  static_cast<void>(
      std::move(configured).RegistrationCloudFactory([](auto const&) {
        return ae::Cloud::ptr{};
      }));
#endif
#if AE_SUPPORT_CLOUD_DNS
  static_cast<void>(std::move(configured).DnsResolverFactory([](auto const&) {
    return ae::DnsResolver::ptr{};
  }));
#endif
  return ae::AetherApp::Construct(std::move(configured));
}
struct TelemetryScenarioBefore {
  std::vector<ae::TelemetryBatch> pending;
  std::string product_space;
  std::uint64_t product_count{};
};
inline std::uint64_t ScenarioMetricCount(ae::TeleStatisticsBackend& backend,
                                         std::string_view space) {
  auto decoded = ae::tele::raw::Decode(backend.Snapshot(space));
  std::uint64_t result{};
  for (auto const& record : decoded.records)
    if (record.metric) result += record.metric->count;
  return result;
}
inline TelemetryScenarioBefore ObserveTelemetryScenario(ae::AetherApp& app,
                                                        std::string space) {
  (void)ae::DrainAetherTelemetry();
  auto statistics = ae::TeleStatistics::ptr{app.aether()->tele_statistics};
  return {statistics->delivery_outbox().pending, space,
          ScenarioMetricCount(*statistics->backend(), space)};
}
inline bool ExportNativeTelemetryArtifact(
    ae::AetherApp& app, std::filesystem::path const& output,
    TelemetryScenarioBefore const& before) {
  AetherTelemetryDelivery checkpoint{ae::AeContext{app}, ae::Client::ptr{}, ""};
  if (!checkpoint.Tick()) return false;
  auto statistics = ae::TeleStatistics::ptr{app.aether()->tele_statistics};
  if (!statistics->PrepareDelivery()) return false;
  auto const& pending = statistics->delivery_outbox().pending;
  if (pending.size() <= before.pending.size()) return false;
  for (std::size_t index = 0; index < before.pending.size(); ++index)
    if (pending[index].id != before.pending[index].id ||
        pending[index].artifact != before.pending[index].artifact)
      return false;
  auto const after_count =
      ScenarioMetricCount(*statistics->backend(), before.product_space);
  if (after_count <= before.product_count) return false;
  ae::DomainGraph graph{statistics->domain};
  if (!graph.SaveRoot(statistics.Load(), statistics.id()).IsOk()) return false;
  auto write = [&](std::filesystem::path const& path,
                   std::vector<std::uint8_t> const& artifact) {
    if (!ae::ValidTelemetryArtifact(artifact)) return false;
    std::ofstream file{path, std::ios::binary};
    file.write(reinterpret_cast<char const*>(artifact.data()),
               static_cast<std::streamsize>(artifact.size()));
    return bool(file);
  };
  if (!write(output / "capture-native.aetlog", pending.front().artifact) ||
      !write(output / "capture-native-new.aetlog", pending.back().artifact))
    return false;
  std::ofstream report{output / "restart-proof.json"};
  report << "{\"prior_pending\":" << before.pending.size()
         << ",\"pending\":" << pending.size()
         << ",\"prior_product_metric_count\":" << before.product_count
         << ",\"product_metric_count\":" << after_count
         << ",\"old_batches_retained\":true,\"new_delta\":true}\n";
  return bool(report);
}
}  // namespace apptraverse
