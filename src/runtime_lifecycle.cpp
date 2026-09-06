#include "apptraverse/runtime_lifecycle.h"

#include <cassert>

#include "apptraverse/object_macros.h"

namespace apptraverse {
namespace {

APPTRAVERSE_REGISTER(ApplicationRuntimeState);
APPTRAVERSE_REGISTER(NetworkState);
APPTRAVERSE_REGISTER(AetherRegistrationState);
APPTRAVERSE_REGISTER(ApplicationStartedEvent);
APPTRAVERSE_REGISTER(NetworkInitializingEvent);
APPTRAVERSE_REGISTER(NetworkInterfaceUnavailableEvent);
APPTRAVERSE_REGISTER(InternetUnavailableEvent);
APPTRAVERSE_REGISTER(NetworkAvailableEvent);
APPTRAVERSE_REGISTER(AetherRegistrationStartedEvent);
APPTRAVERSE_REGISTER(AetherRegistrationCompletedEvent);

}  // namespace

void ForceLifecycleRegistration() {}

bool ApplicationRuntimeState::CanApply(
    ApplicationStartedEvent const& event) const {
  return event.run_id != 0 && event.run_id != run_id;
}

void ApplicationRuntimeState::Apply(ApplicationStartedEvent const& event) {
  assert(CanApply(event));
  run_id = event.run_id;
  NoteMaterializedChange();
}

bool NetworkState::CanApply(NetworkInitializingEvent const& event) const {
  return event.run_id != 0 &&
         (event.run_id != run_id ||
          GetAvailability() != NetworkAvailability::kInitializing);
}

bool NetworkState::CanApply(
    NetworkInterfaceUnavailableEvent const& event) const {
  return event.run_id != 0 && event.run_id == run_id &&
         GetAvailability() != NetworkAvailability::kInterfaceUnavailable;
}

bool NetworkState::CanApply(InternetUnavailableEvent const& event) const {
  return event.run_id != 0 && event.run_id == run_id &&
         GetAvailability() != NetworkAvailability::kInternetUnavailable;
}

bool NetworkState::CanApply(NetworkAvailableEvent const& event) const {
  return event.run_id != 0 && event.run_id == run_id &&
         GetAvailability() != NetworkAvailability::kAvailable;
}

void NetworkState::Apply(NetworkInitializingEvent const& event) {
  assert(CanApply(event));
  run_id = event.run_id;
  availability =
      static_cast<std::uint8_t>(NetworkAvailability::kInitializing);
  NoteMaterializedChange();
}

void NetworkState::Apply(NetworkInterfaceUnavailableEvent const& event) {
  assert(CanApply(event));
  availability =
      static_cast<std::uint8_t>(NetworkAvailability::kInterfaceUnavailable);
  NoteMaterializedChange();
}

void NetworkState::Apply(InternetUnavailableEvent const& event) {
  assert(CanApply(event));
  availability =
      static_cast<std::uint8_t>(NetworkAvailability::kInternetUnavailable);
  NoteMaterializedChange();
}

void NetworkState::Apply(NetworkAvailableEvent const& event) {
  assert(CanApply(event));
  availability = static_cast<std::uint8_t>(NetworkAvailability::kAvailable);
  NoteMaterializedChange();
}

bool AetherRegistrationState::CanApply(
    AetherRegistrationStartedEvent const& event) const {
  return event.run_id != 0 &&
         (event.run_id != run_id ||
          GetPhase() != AetherRegistrationPhase::kRegistering);
}

bool AetherRegistrationState::CanApply(
    AetherRegistrationCompletedEvent const& event) const {
  if (event.uid.empty()) {
    return false;
  }
  if (run_id != 0 && event.run_id != 0 && event.run_id != run_id) {
    return false;
  }
  return !IsRegisteredForCurrentRun() || uid != event.uid;
}

void AetherRegistrationState::Apply(
    AetherRegistrationStartedEvent const& event) {
  assert(CanApply(event));
  run_id = event.run_id;
  phase = static_cast<std::uint8_t>(AetherRegistrationPhase::kRegistering);
  NoteMaterializedChange();
}

void AetherRegistrationState::Apply(
    AetherRegistrationCompletedEvent const& event) {
  assert(CanApply(event));
  if (run_id == 0) {
    run_id = event.run_id;
  }
  registered_run_id = event.run_id;
  uid = event.uid;
  phase = static_cast<std::uint8_t>(AetherRegistrationPhase::kRegistered);
  NoteMaterializedChange();
}

}  // namespace apptraverse
