#pragma once
#include "aether-tele/raw/sink.h"
#include "aether-tele/tele.h"
#ifdef AE_PRODUCT_TELEMETRY_CATALOG
#  include "telemetry_catalog.h"
#endif
namespace apptraverse {
struct AppTraverseTeleIdentity {
  static constexpr std::string_view kName = "apptraverse";
#ifdef AE_PRODUCT_TELEMETRY_CATALOG
  static std::uint32_t ResolveIndex(std::string_view file, std::uint32_t line,
                                    ae::tele::Tag const&) {
    return ae::telemetry_catalog::ResolveIndex(kName, file, line);
  }
#endif
};
struct AppTraverseTeleConfig : ae::tele::AllEnabledConfig {};
using AppTraverseTeleSink =
    ae::tele::RawTeleSink<AppTraverseTeleIdentity, AppTraverseTeleConfig, 4096,
                          1024>;
}  // namespace apptraverse
#define APPTRAVERSE_TELE_DEBUG(TAG, ...) \
  AE_TELE_DEBUG_TO(::apptraverse::AppTraverseTeleSink, TAG, __VA_ARGS__)
#define APPTRAVERSE_TELE_INFO(TAG, ...) \
  AE_TELE_INFO_TO(::apptraverse::AppTraverseTeleSink, TAG, __VA_ARGS__)
#define APPTRAVERSE_TELE_WARNING(TAG, ...) \
  AE_TELE_WARNING_TO(::apptraverse::AppTraverseTeleSink, TAG, __VA_ARGS__)
#define APPTRAVERSE_TELE_ERROR(TAG, ...) \
  AE_TELE_ERROR_TO(::apptraverse::AppTraverseTeleSink, TAG, __VA_ARGS__)
AE_TELE_MODULE(kAppTraverseTelemetry, 41, 0, 65535);
AE_TAG(ModelRuntimeStarted, kAppTraverseTelemetry)
AE_TAG(ModelRuntimeStopped, kAppTraverseTelemetry)
AE_TAG(ModelWorkExecuted, kAppTraverseTelemetry)
AE_TAG(ModelPublication, kAppTraverseTelemetry)
AE_TAG(HistoricalCaptureFailed, kAppTraverseTelemetry)
AE_TAG(HistoricalHydrationFailed, kAppTraverseTelemetry)
AE_TAG(DomainStorageStats, kAppTraverseTelemetry)

AE_TAG(PersistentSnapshotSaved, kAppTraverseTelemetry)
