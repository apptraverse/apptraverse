#include "apptraverse/aether_connection.h"
#include <cstdlib>
#include <cstdarg>
#include "apptraverse/aether_telemetry.h"
#include "apptraverse/aether_telemetry_delivery.h"

#include <map>

#include "aether/ae_actions/select_client.h"
#include "aether/aether_app.h"
#include "aether/config.h"
#include "aether/global_ids.h"
#if AE_SUPPORT_P2P
#  include "aether/peer/peer_types.h"
#endif
#include "apptraverse/aether_link.h"
#include "apptraverse/directory_domain_storage.h"
namespace apptraverse {
namespace {
#if AE_SUPPORT_P2P && AE_SUPPORT_HOLE_PUNCH
std::optional<ae::RendezvousSettings> ReadRendezvous(ae::Uid const& self) {
  constexpr std::string_view test_service = "4bb28061-35e4-47dc-bfd7-1bbfae68b22b";
  auto const* setting = std::getenv("APPTRAVERSE_AETHER_RENDEZVOUS_UID");
  std::string_view value = setting ? std::string_view{setting} : test_service;
  if (!setting) {
    if (auto const* legacy = std::getenv("APPTRAVERSE_AETHER_RENDEZVOUS")) {
      value = std::string_view{legacy};
      value = value.substr(0, value.find('@'));
    }
  }
  constexpr std::size_t uid_size = 36;
  if (value.size() != uid_size) return {};
  for (std::size_t i = 0; i < uid_size; ++i) {
    auto const c = value[i];
    bool const dash = i == 8 || i == 13 || i == 18 || i == 23;
    if (dash ? c != '-'
             : !((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
                 (c >= 'A' && c <= 'F'))) return {};
  }
  auto const uid = ae::Uid::FromString(ae::UidString{value});
  if (uid.empty() || uid == self) return {};
  return ae::RendezvousSettings{uid};
}
#endif
}  // namespace

class AetherConnection {
 public:
  std::filesystem::path profile;
  std::unique_ptr<ae::AetherApp> sdk;
  ae::Client::ptr client;
  ae::Subscription selection;
  std::unique_ptr<AetherByteLink> link;
  std::unique_ptr<AetherTelemetryDelivery> telemetry;
  std::string telemetry_keeper_uid;
  bool telemetry_owner{true};
  bool connect_client{false};
  std::string client_name;
  std::shared_ptr<std::atomic<bool>> network_requested;
  bool selected{false}, failed{false}, presence_enabled{false};
  NetworkObservation network;
  std::string endpoint;
  std::uint64_t started{0}, serviced{0};
  struct Confirmation {
    std::uint64_t monotonic{0};
    std::int64_t utc{0};
  };
  std::map<std::string, Confirmation> confirmations;
  explicit AetherConnection(std::filesystem::path path, std::string name,
                            std::string recipient, bool owner, bool connect,
                            std::shared_ptr<std::atomic<bool>> requested)
      : profile{std::move(path)},
        telemetry_keeper_uid{
#if APPTRAVERSE_AETHER_TELEMETRY_UPLOAD
            std::move(recipient)
#endif
        },
        telemetry_owner{owner},
        client_name{std::move(name)},
        network_requested{requested
                              ? std::move(requested)
                              : std::make_shared<std::atomic<bool>>(connect)} {
#if !APPTRAVERSE_AETHER_TELEMETRY_UPLOAD
    (void)recipient;
#endif
    if (!RegisterAppTraverseTelemetry()) {
      failed = true;
      return;
    }
    sdk = ae::AetherApp::Construct(
        ae::AetherAppContext{[this]() -> std::unique_ptr<ae::IDomainStorage> {
#ifdef APPTRAVERSE_AETHER_TELEMETRY_DISK_BYTES
          return std::make_unique<DirectoryDomainStorage>(
              profile, DirectoryDomainStorage::ObjectBudget{
                           ae::GlobalId::kTeleStatistics,
                           APPTRAVERSE_AETHER_TELEMETRY_DISK_BYTES});
#else
          return std::make_unique<DirectoryDomainStorage>(profile);
#endif
        }}.TelemetryOwner(telemetry_owner));
    if (!sdk) {
      failed = true;
      return;
    }
    if (telemetry_owner)
      telemetry = std::make_unique<AetherTelemetryDelivery>(
          ae::AeContext{*sdk}, ae::Client::ptr{}, telemetry_keeper_uid);
    StartSelection();
  }
  void StartSelection() {
    if (connect_client || !network_requested->load()) return;
    connect_client = true;
    started = serviced;
    selection =
        sdk->aether()
            ->SelectClient(
                ae::Uid::FromString("3ac93165-3d37-4970-87a6-fa4ee27744e4"),
                client_name)
            .result_event()
            .Subscribe([this](auto const& result) {
              selected = true;
              failed = !result;
              if (result) client = result.value();
            });
  }
  ~AetherConnection() {
    telemetry.reset();
    link.reset();
    selection.Reset();
    client = {};
    sdk.reset();
  }
  void Service(ConnectionTime time) {
    if (serviced == time.monotonic_us) return;
    if (failed) {
      if (telemetry) (void)telemetry->Tick();
      return;
    }
    serviced = time.monotonic_us;
    if (!started) started = serviced;
    StartSelection();
    sdk->Update(ae::Now());
    if (telemetry) (void)telemetry->Tick();
    if (!connect_client) return;
    if (sdk->IsExited() || (!selected && serviced - started > 120000000ULL)) {
      failed = true;
      return;
    }
    if (client.is_valid() && !link) {
#if AE_SUPPORT_P2P
      ae::PeerSettings peer_settings;
      peer_settings.app_id =
          ae::Uid::FromString("3ac93165-3d37-4970-87a6-fa4ee27744e4");
#if AE_SUPPORT_HOLE_PUNCH
      peer_settings.rendezvous = ReadRendezvous(client->uid());
#endif
      client->ConfigurePeer(std::move(peer_settings));
#endif
      client->connectivity_policy()
          .Load()
          ->ConfigureRxTimings()
          .ForAllPriorities(
              ae::RxTimingConf::Every(std::chrono::milliseconds{1000}));
      sdk->aether().Save();
      AetherLinkOptions options;
      options.use_presence = presence_enabled;
      options.explicit_presence_watches = true;
      options.gate_delivery_on_presence = false;
      link = std::make_unique<AetherByteLink>(*sdk->aether(), client, options);
      endpoint = link->EndpointId();
      if (telemetry_owner)
        telemetry = std::make_unique<AetherTelemetryDelivery>(
            ae::AeContext{*sdk}, client, telemetry_keeper_uid);
    }
    if (link) link->Service(serviced);
    if (telemetry) (void)telemetry->Tick();
  }
  TransportPhase Phase() const {
    if (failed) return TransportPhase::Failed;
    if (!connect_client) return TransportPhase::Stopped;
    if (!link) return TransportPhase::Starting;
    return network.network != Reachability::Unavailable &&
                   network.internet != Reachability::Unavailable &&
                   client->IsLocallyOnline()
               ? TransportPhase::Connected
               : TransportPhase::Disconnected;
  }
  void NetworkChanged(NetworkObservation v) {
    if (v == network) return;
    bool restored = (v.network == Reachability::Available &&
                     network.network != v.network) ||
                    (v.internet == Reachability::Available &&
                     network.internet != v.internet);
    network = v;
    if (restored && link) link->NetworkAvailable();
  }
  PresenceObservation Observe(PresenceTarget const& target,
                              ConnectionTime time) {
    PresenceObservation result{target, "aether.native", PresenceState::Unknown,
                               0, 5000000};
    if (target.scope != PresenceScope::Endpoint || !link) return result;
    auto availability = Phase() == TransportPhase::Connected
                            ? link->ObservedPresence(target.id)
                            : LinkAvailability::Unknown;
    result.state =
        availability == LinkAvailability::Online    ? PresenceState::Online
        : availability == LinkAvailability::Offline ? PresenceState::Offline
                                                    : PresenceState::Unknown;
    auto confirmed = link->PresenceQueryStarted(target.id);
    auto& record = confirmations[target.id];
    if (result.state != PresenceState::Unknown && confirmed &&
        confirmed != record.monotonic) {
      record.monotonic = confirmed;
      record.utc = time.utc_us -
                   static_cast<std::int64_t>(time.monotonic_us - confirmed);
    }
    result.confirmed_utc_us = record.utc;
    return result;
  }
};
namespace {
class Transport final : public IConnectionTransport {
  std::shared_ptr<AetherConnection> connection_;

 public:
  explicit Transport(std::shared_ptr<AetherConnection> c)
      : connection_{std::move(c)} {}
  void Service(ConnectionTime t) override { connection_->Service(t); }
  void NetworkChanged(NetworkObservation n) override {
    connection_->NetworkChanged(n);
  }
  bool Ready() const override { return connection_->link != nullptr; }
  TransportPhase Phase() const override { return connection_->Phase(); }
  std::string const& EndpointId() const override {
    return connection_->endpoint;
  }
  std::string Name() const override { return "aether"; }
  bool WatchPeer(std::string const& id) override {
    return Ready() && connection_->link->WatchPeer(id);
  }
  bool ConfigureRawService(std::string const& id, Receiver receiver,
                           std::function<void()> reset) override {
    if (!Ready()) return false;
    connection_->link->ConfigureRawService(id);
    connection_->link->SetRawServiceReceiver(std::move(receiver), std::move(reset));
    return connection_->link->WatchPeer(id);
  }
  void SetReceiver(Receiver r) override {
    connection_->link->SetReceiver(std::move(r));
  }
  bool Send(std::string const& id,
            std::vector<std::uint8_t> const& data) override {
    return connection_->link->Send(id, data);
  }
  LinkAvailability Availability(std::string const&) const override {
    return LinkAvailability::Unknown;
  }
  Path PeerPath(std::string const& id) const override {
    auto s = connection_->link->PeerStats(id);
    return {s.linked_tcp_endpoint, s.linked_udp_endpoint};
  }
};
class Presence final : public IPresenceSource {
  std::shared_ptr<AetherConnection> connection_;
  std::vector<PresenceTarget> targets_;
  std::vector<PresenceReading> observations_;

 public:
  explicit Presence(std::shared_ptr<AetherConnection> c)
      : connection_{std::move(c)} {
    connection_->presence_enabled = true;
  }
  ~Presence() override {
    if (connection_->link) {
      for (auto const& target : targets_) {
        if (target.scope == PresenceScope::Endpoint)
          connection_->link->SetPresenceWatch(target.id, false);
      }
    }
  }
  void Watch(PresenceTarget t) override {
    if (std::find(targets_.begin(), targets_.end(), t) == targets_.end())
      targets_.push_back(std::move(t));
  }
  void Unwatch(PresenceTarget const& target) override {
    std::erase(targets_, target);
    if (connection_->link && target.scope == PresenceScope::Endpoint)
      connection_->link->SetPresenceWatch(target.id, false);
  }
  void Service(ConnectionTime time) override {
    connection_->Service(time);
    observations_.clear();
    for (auto const& target : targets_) {
      if (connection_->link && target.scope == PresenceScope::Endpoint)
        connection_->link->SetPresenceWatch(target.id, true);
      auto value = connection_->Observe(target, time);
      auto confirmed = connection_->link
                           ? connection_->link->PresenceQueryStarted(target.id)
                           : 0;
      observations_.push_back({std::move(value), confirmed});
    }
  }
  void NetworkChanged(NetworkObservation n) override {
    connection_->NetworkChanged(n);
  }
  std::vector<PresenceReading> Observations() const override {
    return observations_;
  }
};
}  // namespace
std::shared_ptr<AetherConnection> OpenAetherConnection(
    std::filesystem::path profile, std::string name,
    std::string telemetry_keeper_uid, bool telemetry_owner, bool connect_client,
    std::shared_ptr<std::atomic<bool>> network_requested) {
  return std::make_shared<AetherConnection>(
      std::move(profile), std::move(name), std::move(telemetry_keeper_uid),
      telemetry_owner, connect_client, std::move(network_requested));
}
std::unique_ptr<IConnectionTransport> MakeAetherTransport(
    std::shared_ptr<AetherConnection> c) {
  return std::make_unique<Transport>(std::move(c));
}
std::unique_ptr<IPresenceSource> MakeAetherPresence(
    std::shared_ptr<AetherConnection> c) {
  return std::make_unique<Presence>(std::move(c));
}
ConnectionAdapters MakeAetherConnectionAdapters(
    std::filesystem::path profile, std::string name,
    std::string telemetry_keeper_uid, bool telemetry_owner, bool connect_client,
    std::shared_ptr<std::atomic<bool>> network_requested) {
  auto c = OpenAetherConnection(
      std::move(profile), std::move(name), std::move(telemetry_keeper_uid),
      telemetry_owner, connect_client, std::move(network_requested));
  return {MakeAetherTransport(c), MakeAetherPresence(c)};
}
}  // namespace apptraverse
