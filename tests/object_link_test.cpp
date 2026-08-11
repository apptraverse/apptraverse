#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <type_traits>

#include "aether/clock.h"
#include "aether/domain_storage/ram_domain_storage.h"
#include "aether/obj/obj.h"

#include "apptraverse/node_for.h"
#include "apptraverse/object_link.h"
#include "apptraverse/object_macros.h"

namespace apptraverse::test {

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"       \
                << __LINE__ << '\n';                                         \
      std::exit(1);                                                          \
    }                                                                        \
  } while (0)

class LinkProbe : public NodeFor<LinkProbe> {
  APPTRAVERSE_OBJECT(LinkProbe, Node, 0)

 protected:
  LinkProbe() = default;

 public:
  explicit LinkProbe(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(label), AE_MMBR(shared_peer), AE_MMBR(local_peer))

  std::string label;
  SharedPtr<LinkProbe> shared_peer;
  LocalPtr<LinkProbe> local_peer;
};

APPTRAVERSE_REGISTER(LinkProbe);

void TestTypeDistinction() {
  static_assert(!std::is_same_v<SharedPtr<LinkProbe>, LocalPtr<LinkProbe>>);
  static_assert(SharedPtr<LinkProbe>::kScope == LinkScope::kShared);
  static_assert(LocalPtr<LinkProbe>::kScope == LinkScope::kLocal);
  static_assert(IsSharedPtr<SharedPtr<LinkProbe>>::value);
  static_assert(IsLocalPtr<LocalPtr<LinkProbe>>::value);
  static_assert(!IsSharedPtr<LocalPtr<LinkProbe>>::value);
  static_assert(!IsLocalPtr<SharedPtr<LinkProbe>>::value);
  static_assert(!IsSharedPtr<ae::ObjPtr<LinkProbe>>::value);
}

void TestSharedAndLocalRoundtrip() {
  ae::RamDomainStorage storage;
  ae::Domain domain{ae::Now(), storage};

  auto owner = LinkProbe::ptr::Create(ae::CreateWith{domain}.with_id(10));
  auto peer = LinkProbe::ptr::Create(ae::CreateWith{domain}.with_id(11));
  peer->label = "peer";
  owner->label = "owner";
  owner->shared_peer = peer;
  owner->local_peer = peer;
  owner.Save();

  auto const shared_id = owner->shared_peer.id();
  auto const local_id = owner->local_peer.id();
  CHECK(shared_id.id() == 11);
  CHECK(local_id.id() == 11);
  CHECK(shared_id == local_id);
  CHECK(owner->shared_peer.domain() == &domain);
  CHECK(owner->local_peer.domain() == &domain);

  ae::Domain domain2{ae::Now(), storage};
  auto loaded = LinkProbe::ptr::Declare(ae::CreateWith{domain2}.with_id(10));
  loaded.Load();
  CHECK(loaded.is_loaded());
  CHECK(loaded->shared_peer.id() == shared_id);
  CHECK(loaded->local_peer.id() == local_id);
  CHECK(loaded->shared_peer.domain() == &domain2);
  CHECK(loaded->local_peer.domain() == &domain2);

  loaded->shared_peer.Load();
  loaded->local_peer.Load();
  CHECK(loaded->shared_peer.is_loaded());
  CHECK(loaded->local_peer.is_loaded());
  CHECK(loaded->shared_peer->label == "peer");
  CHECK(loaded->local_peer->label == "peer");
}

void TestAssignmentKeepsCompileTimeScope() {
  ae::RamDomainStorage storage;
  ae::Domain domain{ae::Now(), storage};

  auto a = LinkProbe::ptr::Create(ae::CreateWith{domain}.with_id(20));
  auto b = LinkProbe::ptr::Create(ae::CreateWith{domain}.with_id(21));
  ae::ObjPtr<LinkProbe> raw = b;

  SharedPtr<LinkProbe> shared = raw;
  SharedPtr<LinkProbe> shared2 = shared;
  LocalPtr<LinkProbe> local = raw;
  LocalPtr<LinkProbe> local_from_shared = shared;

  CHECK(shared.id().id() == 21);
  CHECK(shared2.id().id() == 21);
  CHECK(local.id().id() == 21);
  CHECK(local_from_shared.id().id() == 21);
  static_assert(decltype(shared)::kScope == LinkScope::kShared);
  static_assert(decltype(local)::kScope == LinkScope::kLocal);
  static_assert(decltype(local_from_shared)::kScope == LinkScope::kLocal);

  a->shared_peer = raw;
  a->local_peer = shared;
  CHECK(a->shared_peer.id().id() == 21);
  CHECK(a->local_peer.id().id() == 21);
}

}  // namespace apptraverse::test

int main() {
  apptraverse::EnsureObjectRegistration();
  apptraverse::test::TestTypeDistinction();
  apptraverse::test::TestSharedAndLocalRoundtrip();
  apptraverse::test::TestAssignmentKeepsCompileTimeScope();
  std::cout << "object_link_test OK\n";
  return 0;
}
