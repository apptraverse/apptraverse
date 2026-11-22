#pragma once
#include <chrono>
#include <cstdlib>
#include <memory>
#include <string>
#include "aether/aether.h"
#include "aether/aether_tele.h"
#include "aether/tele_statistics.h"
#include "aether/telemetry_delivery.h"
namespace apptraverse {
inline std::string TelemetryKeeperFromEnvironment(char const* variable) {
  auto const* configured = std::getenv(variable);
  return configured ? std::string{configured} : std::string{};
}
// Runtime-only state, serviced on the existing SDK owner thread. Empty
// recipient deliberately leaves upload disabled. This object never mutates the
// UI model.
class AetherTelemetryDelivery {
 public:
  AetherTelemetryDelivery(ae::AeContext const& context,
                          ae::Client::ptr const& client,
                          std::string const& recipient) {
#if AE_TELE_ENABLED && AE_TELE_LOG_TO_STATISTICS
    statistics_ = ae::TeleStatistics::ptr{context.aether().tele_statistics};
    if (statistics_->backend() != ae::AetherTeleSink::Instance().GetBackend()) {
      statistics_ = {};
      return;  // Only the process owner persists the global collector.
    }
    if (recipient.empty() || !client.is_valid()) return;
    // Validate canonical UUID before Uid::FromString (SDK parser requires it).
    if (recipient.size() != 36) return;
    for (std::size_t index = 0; index < recipient.size(); ++index) {
      auto c = recipient[index];
      bool dash = index == 8 || index == 13 || index == 18 || index == 23;
      if (dash ? c != '-'
               : !((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
                   (c >= 'A' && c <= 'F')))
        return;
    }
    auto const destination = ae::Uid::FromString(recipient);
    if (destination.empty()) return;
    session_ = std::make_unique<ae::TelemetryDeliverySession>(
        context, client.Load(), destination, statistics_->delivery_outbox(),
        [this] {
          // Accepted immutable batches must persist even when a full journal
          // temporarily refuses a newer raw tail. Delivery frees that capacity.
          (void)ae::DrainAetherTelemetry();
          ae::DomainGraph graph{statistics_->domain};
          return graph.SaveRoot(statistics_.Load(), statistics_.id()).IsOk();
        });
#else
    (void)context;
    (void)client;
    (void)recipient;
#endif
  }
  bool Tick() {
#if AE_TELE_ENABLED && AE_TELE_LOG_TO_STATISTICS
    if (!statistics_.is_valid()) return true;
    auto now = std::chrono::steady_clock::now();
    if (now >= next_capture_) {
      if (session_ && statistics_->PrepareDelivery()) session_->Pump();
      (void)ae::DrainAetherTelemetry();
      ae::DomainGraph graph{statistics_->domain};
      bool const saved =
          graph.SaveRoot(statistics_.Load(), statistics_.id()).IsOk();
      next_capture_ = now + std::chrono::seconds{30};
      return saved;
    }
#endif
    return true;
  }

 private:
#if AE_TELE_ENABLED && AE_TELE_LOG_TO_STATISTICS
  ae::TeleStatistics::ptr statistics_;
  std::unique_ptr<ae::TelemetryDeliverySession> session_;
  std::chrono::steady_clock::time_point next_capture_{};
#endif
};
}  // namespace apptraverse
