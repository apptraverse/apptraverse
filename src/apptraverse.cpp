#include "apptraverse/object_macros.h"

#include "apptraverse/event.h"
#include "apptraverse/node.h"
#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/presenter.h"

namespace apptraverse {
namespace {

APPTRAVERSE_REGISTER(Event);
APPTRAVERSE_REGISTER(Node);
APPTRAVERSE_REGISTER(Presenter);

void ConfigurePresenterFactories() {
  auto& registry = ae::Registry::GetRegistry();
  for (auto& [class_id, factory] : registry.factories) {
    if (registry.GenerationDistance(Presenter::kClassId, class_id) < 0) {
      continue;
    }
    ae::Factory* runtime_factory = nullptr;
    for (auto& [candidate_id, candidate] : registry.factories) {
      if (registry.relations.count(candidate_id) != 0 ||
          registry.GenerationDistance(class_id, candidate_id) < 0) {
        continue;
      }
      if (runtime_factory != nullptr) {
        // Multiple platform implementations require an explicit choice. Never
        // select an arbitrary descendant according to hash-map iteration.
        runtime_factory = nullptr;
        break;
      }
      runtime_factory = &candidate;
    }
    if (runtime_factory != nullptr) {
      // Presenter subclasses add runtime behavior, while the published model
      // stores the platform-neutral class layers. Specialize construction only;
      // deserialize with the exact stored class's original loader.
      factory.create = runtime_factory->create;
    }
  }
}

}  // namespace

void ForceLifecycleRegistration();
void ForceLinkRegistration();
void ForceSharedNodeRegistration();

// Forces the static library object file (and its Registrars) to be linked.
void EnsureObjectRegistration() {
  EnableNoninteractiveCrt();
  ForceLifecycleRegistration();
  ForceLinkRegistration();
  ForceSharedNodeRegistration();
  // All linked Registrar initializers have completed before the application's
  // first call. Thread-safe once initialization precedes model/UI workers.
  static bool const presenters_configured = [] {
    ConfigurePresenterFactories();
    return true;
  }();
  (void)presenters_configured;
}

}  // namespace apptraverse
