#include "apptraverse/connection_state.h"
namespace apptraverse {
APPTRAVERSE_REGISTER(ConnectionState);
APPTRAVERSE_REGISTER(ObserveNetworkEvent);
APPTRAVERSE_REGISTER(ObserveSessionEvent);
APPTRAVERSE_REGISTER(ObserveClockEvent);
APPTRAVERSE_REGISTER(ResetConnectionEvent);
void EnsureConnectionRegistration() {}
void ConnectionState::ObserveNetwork(NetworkObservation v) {
  if (network == v) return;
  auto e = ObserveNetworkEvent::ptr::Create(ae::CreateWith{*domain});
  e->value = v;
  Commit(e);
}
void ConnectionState::ObserveSession(SessionObservation v) {
  if (session == v) return;
  auto e = ObserveSessionEvent::ptr::Create(ae::CreateWith{*domain});
  e->value = std::move(v);
  Commit(e);
}
void ConnectionState::ObserveClock(std::int64_t v) {
  if (now_utc_us == v) return;
  auto e = ObserveClockEvent::ptr::Create(ae::CreateWith{*domain});
  e->value = v;
  Commit(e);
}
void ConnectionState::Reset() {
  Commit(ResetConnectionEvent::ptr::Create(ae::CreateWith{*domain}));
}
void ConnectionState::Apply(ObserveNetworkEvent const& e) {
  network = e.value;
  NoteMaterializedChange();
}
void ConnectionState::Apply(ObserveClockEvent const& e) {
  now_utc_us = e.value;
  NoteMaterializedChange();
}
void ConnectionState::Apply(ResetConnectionEvent const&) {
  network = {Reachability::Unknown, Reachability::Unknown};
  session = {"", "", TransportPhase::Starting, {}, false, 0, 0};
  NoteMaterializedChange();
}
void ConnectionState::Apply(ObserveSessionEvent const& e) {
  session = e.value;
  for (auto const& p : session.presence) {
    if (p.state != PresenceState::Online || p.confirmed_utc_us <= 0) continue;
    auto it = std::find_if(
        last_online.begin(), last_online.end(), [&](auto const& r) {
          return r.target == p.target && r.source == p.source;
        });
    if (it == last_online.end())
      last_online.push_back({p.target, p.source, p.confirmed_utc_us});
    else
      it->utc_us = std::max(it->utc_us, p.confirmed_utc_us);
  }
  NoteMaterializedChange();
}
std::int64_t ConnectionState::LastOnline(PresenceTarget const& target,
                                         std::string const& source) const {
  for (auto const& p : last_online)
    if (p.target == target && p.source == source) return p.utc_us;
  return 0;
}
}  // namespace apptraverse
