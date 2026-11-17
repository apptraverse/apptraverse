#include "apptraverse/aether_connection.h"

#include <map>

#include "aether/ae_actions/select_client.h"
#include "aether/aether_app.h"
#include "apptraverse/aether_link.h"
#include "apptraverse/directory_domain_storage.h"
#ifdef APPTRAVERSE_MAILBOX_SERVICE_UID
#include "apptraverse/offline_mailbox.h"
#endif
namespace apptraverse {
class AetherConnection {
 public:
  std::filesystem::path profile;
  std::unique_ptr<ae::AetherApp> sdk;
  ae::Client::ptr client;
  ae::Subscription selection;
  std::unique_ptr<AetherByteLink> link;
  bool selected{false}, failed{false}, presence_enabled{false};
  NetworkObservation network;
  std::string endpoint;
  std::uint64_t started{0}, serviced{0};
  struct Confirmation {
    std::uint64_t monotonic{0};
    std::int64_t utc{0};
  };
  std::map<std::string, Confirmation> confirmations;
  explicit AetherConnection(std::filesystem::path path, std::string name)
      : profile{std::move(path)} {
    sdk = ae::AetherApp::Construct(
        ae::AetherAppContext{[this]() -> std::unique_ptr<ae::IDomainStorage> {
          return std::make_unique<DirectoryDomainStorage>(profile);
        }});
    if (!sdk) {
      failed = true;
      return;
    }
    selection =
        sdk->aether()
            ->SelectClient(
                ae::Uid::FromString("3ac93165-3d37-4970-87a6-fa4ee27744e4"),
                name)
            .result_event()
            .Subscribe([this](auto const& result) {
              selected = true;
              failed = !result;
              if (result) client = result.value();
            });
  }
  ~AetherConnection() {
    link.reset();
    selection.Reset();
    client = {};
    sdk.reset();
  }
  void Service(ConnectionTime time) {
    if (failed || serviced == time.monotonic_us) return;
    serviced = time.monotonic_us;
    if (!started) started = serviced;
    sdk->Update(ae::Now());
    if (sdk->IsExited() || (!selected && serviced - started > 120000000ULL)) {
      failed = true;
      return;
    }
    if (client.is_valid() && !link) {
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
    }
    if (link) link->Service(serviced);
  }
  TransportPhase Phase() const {
    if (failed) return TransportPhase::Failed;
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
#ifdef APPTRAVERSE_MAILBOX_SERVICE_UID
// Infrastructure composition only: ChatSession and its application stay unaware
// of mailbox commands. The service itself explicitly selects the direct link.
class MailboxTransport final : public IConnectionTransport {
  std::unique_ptr<IConnectionTransport> direct_;
  std::unique_ptr<mailbox::OfflineMailboxLink> mailbox_;
  Receiver receiver_;
  std::string raw_service_;
 public:
  explicit MailboxTransport(std::unique_ptr<IConnectionTransport> direct)
      : direct_{std::move(direct)} {}
  void Service(ConnectionTime t) override {
    direct_->Service(t);
    if (direct_->Ready() && !mailbox_) {
      mailbox_ = std::make_unique<mailbox::OfflineMailboxLink>(
          *direct_, direct_->EndpointId(), APPTRAVERSE_MAILBOX_SERVICE_UID,
          APPTRAVERSE_MAILBOX_TTL_SECONDS * 1000ULL);
      mailbox_->SetRawServiceEndpoint(raw_service_);
      mailbox_->SetReceiver(receiver_);
    }
    if (mailbox_) mailbox_->Service(t.monotonic_us, t.utc_us);
  }
  void NetworkChanged(NetworkObservation n) override { direct_->NetworkChanged(n); }
  bool Ready() const override { return direct_->Ready() && mailbox_ != nullptr; }
  TransportPhase Phase() const override { return direct_->Phase(); }
  std::string const& EndpointId() const override { return direct_->EndpointId(); }
  std::string Name() const override { return direct_->Name(); }
  bool WatchPeer(std::string const& id) override { return direct_->WatchPeer(id); }
  bool ConfigureRawService(std::string const& id, Receiver receiver,
                           std::function<void()> reset) override {
    if (id == APPTRAVERSE_MAILBOX_SERVICE_UID ||
        !direct_->ConfigureRawService(id, std::move(receiver), std::move(reset))) return false;
    raw_service_ = id;
    if (mailbox_) mailbox_->SetRawServiceEndpoint(id);
    return true;
  }
  Path PeerPath(std::string const& id) const override { return direct_->PeerPath(id); }
  void SetReceiver(Receiver r) override {
    receiver_ = std::move(r);
    if (mailbox_) mailbox_->SetReceiver(receiver_);
  }
  bool Send(std::string const& id, std::vector<std::uint8_t> const& bytes) override {
    return mailbox_ && mailbox_->Send(id, bytes);
  }
  bool SendIdentified(std::string const& id, std::string const& identity,
                      std::vector<std::uint8_t> const& bytes) override {
    return mailbox_ && mailbox_->SendIdentified(id, identity, bytes);
  }
  void ForgetIdentified(std::string const& id, std::string const& identity) override {
    if (mailbox_) mailbox_->ForgetIdentified(id, identity);
  }
  bool SendTransient(std::string const& id, std::vector<std::uint8_t> const& bytes) override {
    return mailbox_ && mailbox_->SendTransient(id, bytes);
  }
  LinkAvailability Availability(std::string const&) const override {
    // A presence reading must not gate store-and-forward admission.
    return LinkAvailability::Unknown;
  }
};
#endif
}  // namespace
std::shared_ptr<AetherConnection> OpenAetherConnection(
    std::filesystem::path profile, std::string name) {
  return std::make_shared<AetherConnection>(std::move(profile),
                                            std::move(name));
}
std::unique_ptr<IConnectionTransport> MakeAetherTransport(
    std::shared_ptr<AetherConnection> c) {
#ifdef APPTRAVERSE_MAILBOX_SERVICE_UID
  return std::make_unique<MailboxTransport>(MakeDirectAetherTransport(std::move(c)));
#else
  return MakeDirectAetherTransport(std::move(c));
#endif
}
std::unique_ptr<IConnectionTransport> MakeDirectAetherTransport(
    std::shared_ptr<AetherConnection> c) {
  return std::make_unique<Transport>(std::move(c));
}
std::unique_ptr<IPresenceSource> MakeAetherPresence(
    std::shared_ptr<AetherConnection> c) {
  return std::make_unique<Presence>(std::move(c));
}
ConnectionAdapters MakeAetherConnectionAdapters(std::filesystem::path profile,
                                                std::string name) {
  auto c = OpenAetherConnection(std::move(profile), std::move(name));
  return {MakeAetherTransport(c), MakeAetherPresence(c)};
}
}  // namespace apptraverse
