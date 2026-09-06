#ifndef APPTRAVERSE_RUNTIME_LIFECYCLE_H_
#define APPTRAVERSE_RUNTIME_LIFECYCLE_H_

#include <cstdint>
#include <stdexcept>
#include <string>

#include "aether-objects/obj/obj.h"

#include "apptraverse/event_for.h"
#include "apptraverse/node_for.h"
#include "apptraverse/object_macros.h"

namespace apptraverse {

enum class NetworkAvailability : std::uint8_t {
  kInitializing = 0,
  kInterfaceUnavailable = 1,
  kInternetUnavailable = 2,
  kAvailable = 3,
};

enum class AetherRegistrationPhase : std::uint8_t {
  kRegistering = 0,
  kRegistered = 1,
};

class ApplicationStartedEvent;
class NetworkInitializingEvent;
class NetworkInterfaceUnavailableEvent;
class InternetUnavailableEvent;
class NetworkAvailableEvent;
class AetherRegistrationStartedEvent;
class AetherRegistrationCompletedEvent;

class ApplicationRuntimeState
    : public NodeFor<ApplicationRuntimeState> {
  APPTRAVERSE_OBJECT(ApplicationRuntimeState, Node, 1)

 protected:
  ApplicationRuntimeState() = default;

 public:
  explicit ApplicationRuntimeState(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(run_id))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv&) {
    throw std::runtime_error(
        "ApplicationRuntimeState v0 is not supported; start with a fresh "
        "state dir");
  }

  template <typename Dnv>
  void Load(ae::Version<1>, Dnv& dnv) {
    Node::Load(ae::Version<1>{}, dnv);
    dnv(run_id);
  }

  template <typename Dnv>
  void Save(ae::Version<1>, Dnv& dnv) const {
    Node::Save(ae::Version<1>{}, dnv);
    dnv(run_id);
  }

  std::uint64_t run_id{0};

  bool CanApply(ApplicationStartedEvent const& event) const;
  void Apply(ApplicationStartedEvent const& event);
};

class NetworkState : public NodeFor<NetworkState> {
  APPTRAVERSE_OBJECT(NetworkState, Node, 1)

 protected:
  NetworkState() = default;

 public:
  explicit NetworkState(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(run_id), AE_MMBR(availability))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv&) {
    throw std::runtime_error(
        "NetworkState v0 is not supported; start with a fresh state dir");
  }

  template <typename Dnv>
  void Load(ae::Version<1>, Dnv& dnv) {
    Node::Load(ae::Version<1>{}, dnv);
    dnv(run_id, availability);
  }

  template <typename Dnv>
  void Save(ae::Version<1>, Dnv& dnv) const {
    Node::Save(ae::Version<1>{}, dnv);
    dnv(run_id, availability);
  }

  std::uint64_t run_id{0};
  std::uint8_t availability{
      static_cast<std::uint8_t>(NetworkAvailability::kInitializing)};

  NetworkAvailability GetAvailability() const {
    return static_cast<NetworkAvailability>(availability);
  }

  bool CanApply(NetworkInitializingEvent const& event) const;
  bool CanApply(NetworkInterfaceUnavailableEvent const& event) const;
  bool CanApply(InternetUnavailableEvent const& event) const;
  bool CanApply(NetworkAvailableEvent const& event) const;
  void Apply(NetworkInitializingEvent const& event);
  void Apply(NetworkInterfaceUnavailableEvent const& event);
  void Apply(InternetUnavailableEvent const& event);
  void Apply(NetworkAvailableEvent const& event);
};

class AetherRegistrationState : public NodeFor<AetherRegistrationState> {
  APPTRAVERSE_OBJECT(AetherRegistrationState, Node, 1)

 protected:
  AetherRegistrationState() = default;

 public:
  explicit AetherRegistrationState(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(run_id), AE_MMBR(registered_run_id), AE_MMBR(phase),
                    AE_MMBR(uid))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv&) {
    throw std::runtime_error(
        "AetherRegistrationState v0 is not supported; start with a fresh "
        "state dir");
  }

  template <typename Dnv>
  void Load(ae::Version<1>, Dnv& dnv) {
    Node::Load(ae::Version<1>{}, dnv);
    dnv(run_id, registered_run_id, phase, uid);
  }

  template <typename Dnv>
  void Save(ae::Version<1>, Dnv& dnv) const {
    Node::Save(ae::Version<1>{}, dnv);
    dnv(run_id, registered_run_id, phase, uid);
  }

  std::uint64_t run_id{0};
  std::uint64_t registered_run_id{0};
  std::uint8_t phase{
      static_cast<std::uint8_t>(AetherRegistrationPhase::kRegistering)};
  std::string uid;

  AetherRegistrationPhase GetPhase() const {
    return static_cast<AetherRegistrationPhase>(phase);
  }

  bool IsRegisteredForCurrentRun() const {
    return GetPhase() == AetherRegistrationPhase::kRegistered &&
           registered_run_id == run_id && !uid.empty();
  }

  // UID is displayable only after this run's registration completed.
  std::string CurrentUid() const {
    if (!IsRegisteredForCurrentRun()) {
      return {};
    }
    return uid;
  }

  bool CanApply(AetherRegistrationStartedEvent const& event) const;
  bool CanApply(AetherRegistrationCompletedEvent const& event) const;
  void Apply(AetherRegistrationStartedEvent const& event);
  void Apply(AetherRegistrationCompletedEvent const& event);
};

class ApplicationStartedEvent
    : public EventFor<ApplicationRuntimeState, ApplicationStartedEvent> {
  APPTRAVERSE_OBJECT(ApplicationStartedEvent, Event, 0)

 protected:
  ApplicationStartedEvent() = default;

 public:
  explicit ApplicationStartedEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(run_id))

  std::uint64_t run_id{0};
};

class NetworkInitializingEvent
    : public EventFor<NetworkState, NetworkInitializingEvent> {
  APPTRAVERSE_OBJECT(NetworkInitializingEvent, Event, 0)

 protected:
  NetworkInitializingEvent() = default;

 public:
  explicit NetworkInitializingEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(run_id))

  std::uint64_t run_id{0};
};

class NetworkInterfaceUnavailableEvent
    : public EventFor<NetworkState, NetworkInterfaceUnavailableEvent> {
  APPTRAVERSE_OBJECT(NetworkInterfaceUnavailableEvent, Event, 0)

 protected:
  NetworkInterfaceUnavailableEvent() = default;

 public:
  explicit NetworkInterfaceUnavailableEvent(ae::ObjProp prop)
      : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(run_id))

  std::uint64_t run_id{0};
};

class InternetUnavailableEvent
    : public EventFor<NetworkState, InternetUnavailableEvent> {
  APPTRAVERSE_OBJECT(InternetUnavailableEvent, Event, 0)

 protected:
  InternetUnavailableEvent() = default;

 public:
  explicit InternetUnavailableEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(run_id))

  std::uint64_t run_id{0};
};

class NetworkAvailableEvent
    : public EventFor<NetworkState, NetworkAvailableEvent> {
  APPTRAVERSE_OBJECT(NetworkAvailableEvent, Event, 0)

 protected:
  NetworkAvailableEvent() = default;

 public:
  explicit NetworkAvailableEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(run_id))

  std::uint64_t run_id{0};
};

class AetherRegistrationStartedEvent
    : public EventFor<AetherRegistrationState, AetherRegistrationStartedEvent> {
  APPTRAVERSE_OBJECT(AetherRegistrationStartedEvent, Event, 0)

 protected:
  AetherRegistrationStartedEvent() = default;

 public:
  explicit AetherRegistrationStartedEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(run_id))

  std::uint64_t run_id{0};
};

class AetherRegistrationCompletedEvent
    : public EventFor<AetherRegistrationState,
                      AetherRegistrationCompletedEvent> {
  APPTRAVERSE_OBJECT(AetherRegistrationCompletedEvent, Event, 0)

 protected:
  AetherRegistrationCompletedEvent() = default;

 public:
  explicit AetherRegistrationCompletedEvent(ae::ObjProp prop)
      : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(run_id), AE_MMBR(uid))

  std::uint64_t run_id{0};
  std::string uid;
};

inline bool CommitApplicationStarted(ApplicationRuntimeState& runtime,
                                     std::uint64_t run_id) {
  auto event =
      ApplicationStartedEvent::ptr::Create(ae::CreateWith{*runtime.domain});
  event->run_id = run_id;
  if (!runtime.CanApply(*event)) {
    return false;
  }
  runtime.Commit(event);
  return true;
}

inline bool CommitNetworkInitializing(NetworkState& network,
                                      std::uint64_t run_id) {
  auto event =
      NetworkInitializingEvent::ptr::Create(ae::CreateWith{*network.domain});
  event->run_id = run_id;
  if (!network.CanApply(*event)) {
    return false;
  }
  network.Commit(event);
  return true;
}

inline bool CommitNetworkInterfaceUnavailable(NetworkState& network,
                                              std::uint64_t run_id) {
  auto event = NetworkInterfaceUnavailableEvent::ptr::Create(
      ae::CreateWith{*network.domain});
  event->run_id = run_id;
  if (!network.CanApply(*event)) {
    return false;
  }
  network.Commit(event);
  return true;
}

inline bool CommitInternetUnavailable(NetworkState& network,
                                      std::uint64_t run_id) {
  auto event =
      InternetUnavailableEvent::ptr::Create(ae::CreateWith{*network.domain});
  event->run_id = run_id;
  if (!network.CanApply(*event)) {
    return false;
  }
  network.Commit(event);
  return true;
}

inline bool CommitNetworkAvailable(NetworkState& network,
                                   std::uint64_t run_id) {
  auto event =
      NetworkAvailableEvent::ptr::Create(ae::CreateWith{*network.domain});
  event->run_id = run_id;
  if (!network.CanApply(*event)) {
    return false;
  }
  network.Commit(event);
  return true;
}

inline bool CommitAetherRegistrationStarted(AetherRegistrationState& aether,
                                            std::uint64_t run_id) {
  auto event = AetherRegistrationStartedEvent::ptr::Create(
      ae::CreateWith{*aether.domain});
  event->run_id = run_id;
  if (!aether.CanApply(*event)) {
    return false;
  }
  aether.Commit(event);
  return true;
}

inline bool CommitAetherRegistrationCompleted(AetherRegistrationState& aether,
                                              std::string uid) {
  if (uid.empty()) {
    return false;
  }
  auto event = AetherRegistrationCompletedEvent::ptr::Create(
      ae::CreateWith{*aether.domain});
  event->run_id = aether.run_id;
  event->uid = std::move(uid);
  if (!aether.CanApply(*event)) {
    return false;
  }
  aether.Commit(event);
  return true;
}

// Every process run must commit a fresh observation session before presenting
// replayed network/Aether state as current.
inline void BeginRuntimeSession(ApplicationRuntimeState& runtime,
                                NetworkState& network,
                                AetherRegistrationState& aether) {
  std::uint64_t run = runtime.run_id + 1;
  if (run == 0) {
    run = 1;
  }
  static_cast<void>(CommitApplicationStarted(runtime, run));
  static_cast<void>(CommitNetworkInitializing(network, run));
  static_cast<void>(CommitAetherRegistrationStarted(aether, run));
}

}  // namespace apptraverse

#endif  // APPTRAVERSE_RUNTIME_LIFECYCLE_H_
