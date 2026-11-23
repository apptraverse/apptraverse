#pragma once
// Optional runtime adapter. The portable model library has no SDK dependency.
#include "aether/tele_statistics_backend.h"
#include "aether/telemetry_namespaces.h"
#include "apptraverse/telemetry.h"
namespace apptraverse {
template <class Sink>
bool BindAetherTelemetry(std::shared_ptr<ae::tele::raw::IBackend> const& target,
                         bool migrate) {
  auto& sink = Sink::Instance();
  if (!migrate) {
    sink.SetRuntime({false, false, false, false, 0});
    return sink.SetBackend(target);
  }
  // Restore the compiled configuration after the previous SDK context exits.
  sink.SetRuntime({true, false, true, true,
                   ae::tele::raw::kTime | ae::tele::raw::kDuration |
                       ae::tele::raw::kPayload});
  return sink.SetBackend(target, [&](auto const& old) {
    if (!old || old == target) return true;
    if (old->TypeToken() == ae::TeleStatisticsBackend::StaticTypeToken() &&
        target->TypeToken() == ae::TeleStatisticsBackend::StaticTypeToken())
      return std::static_pointer_cast<ae::TeleStatisticsBackend>(target)
          ->MergeEarly(
              std::static_pointer_cast<ae::TeleStatisticsBackend>(old));
    auto early = old->Snapshot(Sink::kName);
    return early.empty() || target->Append(Sink::kName, early);
  });
}
template <class Sink>
bool RegisterAetherTelemetrySink(std::string catalog) {
  return ae::RegisterTelemetrySink(std::string{Sink::kName}, std::move(catalog),
                                   BindAetherTelemetry<Sink>,
                                   [] { return Sink::Instance().DrainAll(); });
}
inline bool RegisterAppTraverseTelemetry() {
#if defined(APPTRAVERSE_TELEMETRY_DISABLED)
  return true;
#elif defined(AE_PRODUCT_TELEMETRY_CATALOG)
  return RegisterAetherTelemetrySink<AppTraverseTeleSink>(std::string{
      ae::telemetry_catalog::CatalogId(AppTraverseTeleSink::kName)});
#else
  return RegisterAetherTelemetrySink<AppTraverseTeleSink>("source-callsite-v1");
#endif
}
}  // namespace apptraverse
