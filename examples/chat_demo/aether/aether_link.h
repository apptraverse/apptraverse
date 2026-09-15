#ifndef APPTRAVERSE_EXAMPLE_CHAT_DEMO_AETHER_LINK_H_
#define APPTRAVERSE_EXAMPLE_CHAT_DEMO_AETHER_LINK_H_

#include <string>

#include "aether-miscpp/reflect/reflect.h"
#include "apptraverse/link.h"
#include "apptraverse/node_for.h"
#include "apptraverse/object_macros.h"

namespace apptraverse::example::chat_demo {

// Aether transport descriptor. Runtime transport objects are not persisted;
// only this configuration survives Save/Load.
class AetherLink : public apptraverse::NodeFor<AetherLink, apptraverse::Link> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::example::chat_demo::AetherLink",
                           AetherLink, Link, 0)

 protected:
  AetherLink() = default;

 public:
  explicit AetherLink(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(endpoint_uid))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, endpoint_uid);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, endpoint_uid);
  }

  std::string const& EndpointUid() const override {
    return endpoint_uid;
  }

  std::string endpoint_uid;
};

void EnsureAetherLinkRegistration();

}  // namespace apptraverse::example::chat_demo

#endif  // APPTRAVERSE_EXAMPLE_CHAT_DEMO_AETHER_LINK_H_
