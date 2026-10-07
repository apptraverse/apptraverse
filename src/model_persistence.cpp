#include "apptraverse/model_persistence.h"

#include <unordered_set>

#include "aether-objects/obj/registry.h"

#include "apptraverse/presenter.h"
#include "apptraverse/runtime_lifecycle.h"

namespace apptraverse {
namespace {

std::unordered_set<std::uint32_t>& RuntimeOnlyClassIds() {
  static std::unordered_set<std::uint32_t> ids;
  return ids;
}

void RegisterDefaultRuntimeOnlyClasses() {
  static bool once = false;
  if (once) {
    return;
  }
  once = true;
  RegisterRuntimeOnlyClassId(ApplicationRuntimeState::kClassId);
  RegisterRuntimeOnlyClassId(NetworkState::kClassId);
  RegisterRuntimeOnlyClassId(AetherRegistrationState::kClassId);
  RegisterRuntimeOnlyClassId(ApplicationStartedEvent::kClassId);
  RegisterRuntimeOnlyClassId(NetworkInitializingEvent::kClassId);
  RegisterRuntimeOnlyClassId(NetworkInterfaceUnavailableEvent::kClassId);
  RegisterRuntimeOnlyClassId(InternetUnavailableEvent::kClassId);
  RegisterRuntimeOnlyClassId(NetworkAvailableEvent::kClassId);
  RegisterRuntimeOnlyClassId(AetherRegistrationStartedEvent::kClassId);
  RegisterRuntimeOnlyClassId(AetherRegistrationCompletedEvent::kClassId);
}

}  // namespace

void RegisterRuntimeOnlyClassId(std::uint32_t class_id) {
  RuntimeOnlyClassIds().insert(class_id);
}

bool IsRuntimeOnlyClassId(std::uint32_t class_id) {
  RegisterDefaultRuntimeOnlyClasses();
  return RuntimeOnlyClassIds().count(class_id) != 0;
}

bool IsExcludedFromPersistentSnapshot(ae::Obj const& object) {
  RegisterDefaultRuntimeOnlyClasses();
  auto const class_id = object.GetClassId();
  if (ae::Registry::GetRegistry().GenerationDistance(Presenter::kClassId,
                                                     class_id) >= 0) {
    return true;
  }
  return IsRuntimeOnlyClassId(class_id);
}

}  // namespace apptraverse
