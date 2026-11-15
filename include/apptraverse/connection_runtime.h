#pragma once
#include <functional>
#include <memory>

#include "apptraverse/byte_link.h"
#include "apptraverse/connection_state.h"
namespace apptraverse {
struct ConnectionTime {
  std::uint64_t monotonic_us;
  std::int64_t utc_us;
};
ConnectionTime ReadConnectionTime();
// Worker-affine runtime contracts. No domain, application or UI dependency.
class IConnectionTransport : public IByteLink {
 public:
  virtual void Service(ConnectionTime) = 0;
  virtual void NetworkChanged(NetworkObservation) = 0;
  virtual bool Ready() const = 0;
  virtual TransportPhase Phase() const = 0;
  virtual std::string const& EndpointId() const = 0;
  virtual std::string Name() const = 0;
  virtual bool WatchPeer(std::string const&) = 0;
  // Optional application-owned service channel on the same SDK connection.
  virtual bool ConfigureRawService(std::string const&, Receiver,
                                   std::function<void()>) { return false; }
  struct Path {
    bool tcp{false}, udp{false};
  };
  virtual Path PeerPath(std::string const&) const { return {}; }
};
struct PresenceReading {
  PresenceObservation value;
  // Process-local source clock; never serialized into the graph.
  std::uint64_t observed_monotonic_us;
};
class IPresenceSource {
 public:
  virtual ~IPresenceSource() = default;
  virtual void Service(ConnectionTime) = 0;
  virtual void NetworkChanged(NetworkObservation) = 0;
  virtual void Watch(PresenceTarget) = 0;
  virtual void Unwatch(PresenceTarget const&) = 0;
  virtual std::vector<PresenceReading> Observations() const = 0;
};
struct ConnectionAdapters {
  std::unique_ptr<IConnectionTransport> transport;
  std::unique_ptr<IPresenceSource> presence;
};
// SharedSyncRuntime owns graph protocol/ACK/retry. This runtime owns only the
// independently selected adapters. Presence never gates packet transmission.
class ConnectionRuntime {
 public:
  explicit ConnectionRuntime(ConnectionAdapters adapters);
  IConnectionTransport& Transport() { return *adapters_.transport; }
  void WatchPresence(PresenceTarget target) {
    adapters_.presence->Watch(std::move(target));
  }
  void UnwatchPresence(PresenceTarget const& target) {
    adapters_.presence->Unwatch(target);
  }
  std::vector<PresenceReading> PresenceReadings() const {
    return adapters_.presence->Observations();
  }
  void NetworkChanged(NetworkObservation value);
  void Service(ConnectionTime now);
  SessionObservation Observation() const;

 private:
  ConnectionAdapters adapters_;
  std::uint64_t now_us_{0};
};
}  // namespace apptraverse
