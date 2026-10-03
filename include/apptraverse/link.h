#ifndef APPTRAVERSE_LINK_H_
#define APPTRAVERSE_LINK_H_

#include <cstdint>
#include <string>
#include <stdexcept>

#include "apptraverse/node_for.h"
#include "apptraverse/object_macros.h"

namespace apptraverse {

// Persistent logical connectivity endpoint. Multiple graph objects may
// reference the same Link instance. Locality is runtime-relative and is never
// stored on Link (no is_local field).
class Link : public NodeFor<Link> {
  APPTRAVERSE_OBJECT(Link, Node, 0)

 protected:
  Link() = default;

 public:
  explicit Link(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT()

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_);
  }

  // Transport address of this endpoint, as the concrete descriptor defines it.
  // Locality stays runtime-relative: a runtime recognizes its own Link by
  // comparing this with its own endpoint uid. Empty when the descriptor has no
  // transport address yet.
  //
  // Observed availability is not a field of Link. The transport adapter
  // reports it for this uid at runtime. A saved Online is not reloaded.
  virtual std::string const& EndpointUid() const;
};

// First concrete transport descriptor. Runtime transport objects are not
// persisted; only this configuration survives Save/Load.
class MemoryLink : public NodeFor<MemoryLink, Link> {
  APPTRAVERSE_OBJECT(MemoryLink, Link, 2)

 protected:
  MemoryLink() = default;

 public:
  explicit MemoryLink(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(endpoint_uid), AE_MMBR(heartbeat_interval_ms))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv&) {
    throw std::runtime_error("MemoryLink v0 is not supported");
  }

  template <typename Dnv>
  void Load(ae::Version<1>, Dnv&) {
    throw std::runtime_error(
        "MemoryLink v1 flattened layout is not supported; re-distill with a "
        "fresh state dir");
  }

  template <typename Dnv>
  void Load(ae::Version<2>, Dnv& dnv) {
    dnv(base_, endpoint_uid, heartbeat_interval_ms);
  }

  template <typename Dnv>
  void Save(ae::Version<2>, Dnv& dnv) const {
    dnv(base_, endpoint_uid, heartbeat_interval_ms);
  }

  std::string const& EndpointUid() const override { return endpoint_uid; }

  std::string endpoint_uid;
  std::uint32_t heartbeat_interval_ms{0};
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_LINK_H_
