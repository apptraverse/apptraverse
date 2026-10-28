#ifndef APPTRAVERSE_APPLICATION_MODEL_SESSION_H_
#define APPTRAVERSE_APPLICATION_MODEL_SESSION_H_

#include <functional>
#include <type_traits>

#include "aether-objects/obj/domain.h"
#include "aether-objects/obj/obj_id.h"

#include "apptraverse/application_model_runtime.h"
#include "apptraverse/distill.h"
#include "apptraverse/node.h"

namespace apptraverse {

template <typename TApplication>
struct ApplicationModelSessionHooks {
  std::function<void(TApplication&)> prepare;
  std::function<int(TApplication&)> persist;
  std::function<void(TApplication&)> before_incremental;
};

template <typename TApplication>
int RunApplicationModelSession(
    ae::IDomainStorage& storage, ae::ObjId root_id,
    ApplicationModelRuntime& runtime,
    ApplicationPublishedCallback on_published,
    ApplicationModelSessionHooks<TApplication> const& hooks) {
  static_assert(std::is_base_of_v<Node, TApplication>,
                "RunApplicationModelSession requires a Node application root");

  ae::Domain domain{storage};
  auto root_ref = LoadApplication<TApplication>(domain, root_id);
  TApplication& root = *root_ref;

  hooks.prepare(root);

  BeforeIncrementalPublicationCallback before_incremental;
  if (hooks.before_incremental) {
    before_incremental = [&root, &hooks] { hooks.before_incremental(root); };
  }

  runtime.Run(domain, root, std::move(on_published), std::move(before_incremental));

  return hooks.persist(root);
}

}  // namespace apptraverse

#endif  // APPTRAVERSE_APPLICATION_MODEL_SESSION_H_
