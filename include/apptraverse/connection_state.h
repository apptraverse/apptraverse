#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "apptraverse/event_for.h"
#include "apptraverse/node_for.h"

namespace apptraverse {
// Independent observations. None of these enums is a user-facing status.
enum class Reachability : std::int8_t {
  Unknown = -1,
  Unavailable = 0,
  Available = 1
};
enum class TransportPhase : std::uint8_t {
  Starting,
  Connected,
  Disconnected,
  Failed,
  Stopped
};
enum class PresenceScope : std::uint8_t { Endpoint, Application, Graph };
enum class PresenceState : std::uint8_t { Unknown, Online, Offline };
struct NetworkObservation {
  Reachability network{Reachability::Unknown}, internet{Reachability::Unknown};
  AE_REFLECT_MEMBERS(network, internet)
  bool operator==(NetworkObservation const&) const = default;
};
struct PresenceTarget {
  PresenceScope scope{PresenceScope::Endpoint};
  std::string id;
  AE_REFLECT_MEMBERS(scope, id)
  bool operator==(PresenceTarget const&) const = default;
};
struct PresenceObservation {
  PresenceTarget target;
  std::string source;
  PresenceState state{PresenceState::Unknown};
  // UTC is portable. Monotonic expiry belongs exclusively to the live source.
  std::int64_t confirmed_utc_us{0};
  std::uint64_t valid_for_us{0};
  AE_REFLECT_MEMBERS(target, source, state, confirmed_utc_us, valid_for_us)
  bool operator==(PresenceObservation const&) const = default;
};
struct SessionObservation {
  std::string transport, local_endpoint;
  TransportPhase phase{TransportPhase::Starting};
  std::vector<PresenceObservation> presence;
  bool graph_attached{false};
  std::uint64_t pending_data{0}, pending_progress{0};
  AE_REFLECT_MEMBERS(transport, local_endpoint, phase, presence, graph_attached,
                     pending_data, pending_progress)
  bool operator==(SessionObservation const&) const = default;
};
struct LastOnlineObservation {
  PresenceTarget target;
  std::string source;
  std::int64_t utc_us{0};
  AE_REFLECT_MEMBERS(target, source, utc_us)
  bool operator==(LastOnlineObservation const&) const = default;
};
class ObserveNetworkEvent;
class ObserveSessionEvent;
class ObserveClockEvent;
class ResetConnectionEvent;
class ConnectionState : public NodeFor<ConnectionState> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::ConnectionState", ConnectionState,
                           Node, 0)
 protected:
  ConnectionState() = default;

 public:
  explicit ConnectionState(ae::ObjProp p) : NodeFor{p} {}
  NetworkObservation network;
  SessionObservation session;
  std::vector<LastOnlineObservation> last_online;
  std::int64_t now_utc_us;
  AE_OBJECT_REFLECT(AE_MMBR(network), AE_MMBR(session), AE_MMBR(last_online),
                    AE_MMBR(now_utc_us))
  template <class D>
  void Load(ae::Version<0>, D& d) {
    d(base_, network, session, last_online, now_utc_us);
  }
  template <class D>
  void Save(ae::Version<0>, D& d) const {
    d(base_, network, session, last_online, now_utc_us);
  }
  void ObserveNetwork(NetworkObservation value);
  void ObserveSession(SessionObservation value);
  void ObserveClock(std::int64_t utc_us);
  // New live attachment never trusts persisted online state; keeps last seen.
  void Reset();
  std::int64_t LastOnline(PresenceTarget const&,
                          std::string const& source) const;
  void Apply(ObserveNetworkEvent const&);
  void Apply(ObserveSessionEvent const&);
  void Apply(ObserveClockEvent const&);
  void Apply(ResetConnectionEvent const&);
};
#define APPTRAVERSE_CONNECTION_EVENT(Name, Type, Member)            \
  class Name : public EventFor<ConnectionState, Name> {             \
    APPTRAVERSE_NAMED_OBJECT("apptraverse::" #Name, Name, Event, 0) \
   protected:                                                       \
    Name() = default;                                               \
                                                                    \
   public:                                                          \
    explicit Name(ae::ObjProp p) : EventFor{p} {}                   \
    Type Member;                                                    \
    AE_OBJECT_REFLECT(AE_MMBR(Member))                              \
    template <class D>                                              \
    void Load(ae::Version<0>, D& d) {                               \
      d(base_, Member);                                             \
    }                                                               \
    template <class D>                                              \
    void Save(ae::Version<0>, D& d) const {                         \
      d(base_, Member);                                             \
    }                                                               \
  };
APPTRAVERSE_CONNECTION_EVENT(ObserveNetworkEvent, NetworkObservation, value)
APPTRAVERSE_CONNECTION_EVENT(ObserveSessionEvent, SessionObservation, value)
APPTRAVERSE_CONNECTION_EVENT(ObserveClockEvent, std::int64_t, value)
#undef APPTRAVERSE_CONNECTION_EVENT
class ResetConnectionEvent
    : public EventFor<ConnectionState, ResetConnectionEvent> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::ResetConnectionEvent",
                           ResetConnectionEvent, Event, 0)
 protected:
  ResetConnectionEvent() = default;

 public:
  explicit ResetConnectionEvent(ae::ObjProp p) : EventFor{p} {}
  AE_OBJECT_REFLECT()
  template <class D>
  void Load(ae::Version<0>, D& d) {
    d(base_);
  }
  template <class D>
  void Save(ae::Version<0>, D& d) const {
    d(base_);
  }
};
void EnsureConnectionRegistration();
}  // namespace apptraverse
