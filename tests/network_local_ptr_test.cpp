#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "aether-miscpp/serialization/binary_archive.h"
#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"
#include "apptraverse/object_link.h"
#include "apptraverse/object_macros.h"

namespace apptraverse::test {
namespace {

#define CHECK(condition)                                                     \
  do {                                                                       \
    if (!(condition)) {                                                       \
      std::cerr << "CHECK failed: " #condition << " at " << __FILE__ << ':'   \
                << __LINE__ << '\n';                                         \
      std::exit(1);                                                           \
    }                                                                        \
  } while (false)

class LocalState : public ae::Obj {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::NetworkLocalState", LocalState,
                           ae::Obj, 0)
 protected:
  LocalState() = default;
 public:
  explicit LocalState(ae::ObjProp prop) : Obj{prop} {}
  AE_OBJECT_REFLECT(AE_MMBR(value))
  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) { dnv(base_, value); }
  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const { dnv(base_, value); }
  std::string value;
};

class SenderState : public LocalState {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::NetworkSenderState", SenderState,
                           LocalState, 0)
 protected:
  SenderState() = default;
 public:
  explicit SenderState(ae::ObjProp prop) : LocalState{prop} {}
  AE_OBJECT_REFLECT(AE_MMBR(secret))
  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) { dnv(base_, secret); }
  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const { dnv(base_, secret); }
  std::string secret;
};

class ReceiverState : public LocalState {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::NetworkReceiverState",
                           ReceiverState, LocalState, 0)
 protected:
  ReceiverState() = default;
 public:
  explicit ReceiverState(ae::ObjProp prop) : LocalState{prop} {}
  AE_OBJECT_REFLECT(AE_MMBR(pending))
  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) { dnv(base_, pending); }
  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const { dnv(base_, pending); }
  std::vector<std::uint8_t> pending;
};

class DescriptorBase : public ae::Obj {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::NetworkDescriptorBase",
                           DescriptorBase, ae::Obj, 0)
 protected:
  DescriptorBase() = default;
 public:
  explicit DescriptorBase(ae::ObjProp prop) : Obj{prop} {}
  AE_OBJECT_REFLECT(AE_MMBR(uid), AE_MMBR(local))
  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) { dnv(base_, uid, local); }
  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const { dnv(base_, uid, local); }
  std::string uid;
  LocalPtr<LocalState> local;
};

class Descriptor : public DescriptorBase {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::NetworkDescriptor", Descriptor,
                           DescriptorBase, 0)
 protected:
  Descriptor() = default;
 public:
  explicit Descriptor(ae::ObjProp prop) : DescriptorBase{prop} {}
  AE_OBJECT_REFLECT(AE_MMBR(label), AE_MMBR(peer), AE_MMBR(alias))
  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) { dnv(base_, label, peer, alias); }
  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, label, peer, alias);
  }
  std::string label;
  SharedPtr<Descriptor> peer;
  SharedPtr<Descriptor> alias;
};

APPTRAVERSE_REGISTER(LocalState);
APPTRAVERSE_REGISTER(SenderState);
APPTRAVERSE_REGISTER(ReceiverState);
APPTRAVERSE_REGISTER(DescriptorBase);
APPTRAVERSE_REGISTER(Descriptor);

// This is a native archive of the native layer table, not an object-field wire
// format. The test deliberately does not use publication or remapping helpers.
struct Packet {
  ae::ObjId root;
  ae::RamDomainStorage::ObjClassData layers;
  AE_REFLECT_MEMBERS(root, layers)
};

void BreakCycle(Descriptor& root) {
  auto peer = root.peer.Load();
  peer->peer.Reset();
  peer->alias.Reset();
  root.peer.Reset();
  root.alias.Reset();
}

void TestNetworkLoadPreservesLocalState() {
  ae::ObjId root_id;
  ae::ObjId peer_id;
  ae::ObjId sender_local_id;
  std::vector<std::uint8_t> bytes;
  {
    ae::RamDomainStorage storage;
    ae::Domain domain{storage};
    auto root = Descriptor::ptr::Create(ae::CreateWith{domain});
    auto peer = Descriptor::ptr::Create(ae::CreateWith{domain});
    auto local = SenderState::ptr::Create(ae::CreateWith{domain});
    root_id = root.id();
    peer_id = peer.id();
    sender_local_id = local.id();
    root->uid = "root-uid";
    root->label = "shared root";
    peer->uid = "peer-uid";
    peer->label = "shared peer";
    local->value = "sender local value";
    local->secret = "sender credentials";
    root->local = local;
    peer->local = local;
    root->peer = peer;
    root->alias = peer;
    peer->peer = root;
    peer->alias = root;

    ae::DomainGraph persistent{&domain};
    auto const saved = persistent.SaveRoot(root.Load(), root.id());
    CHECK(saved.IsOk());
    CHECK(storage.state.count(sender_local_id) == 1);

    ae::RamDomainStorage scratch;
    ae::Domain scratch_domain{scratch};
    ae::DomainGraph network{&scratch_domain,
                            ae::GraphSerializationScope::NetworkShared};
    auto const exported = network.SaveRoot(root.Load(), root.id());
    CHECK(exported.IsOk());
    CHECK(scratch.state.count(sender_local_id) == 0);
    CHECK(scratch.state.size() == 2);
    Packet packet{root_id, scratch.state};
    ae::seri::BinaryVectorBuffer buffer{bytes};
    ae::seri::BinaryArchive archive{buffer};
    auto const encoded = archive.Save(packet);
    CHECK(encoded.IsOk());
    BreakCycle(*root);
  }

  Packet packet;
  ae::seri::BinaryVectorBuffer buffer{bytes};
  ae::seri::BinaryArchive archive{buffer};
  auto const decoded = archive.Load(packet);
  CHECK(decoded.IsOk());
  CHECK(archive.buffer().read_position() == bytes.size());
  CHECK(packet.root == root_id);
  ae::RamDomainStorage incoming;
  incoming.state = packet.layers;

  // New network shells have empty local slots. Aliases and the back edge
  // resolve to canonical instances in the independent receiver Domain.
  {
    ae::RamDomainStorage storage = incoming;
    ae::Domain domain{storage};
    ae::DomainGraph graph{&domain,
                          ae::GraphSerializationScope::NetworkShared};
    auto held = graph.LoadRoot(root_id);
    CHECK(held);
    auto root = held.as<Descriptor>();
    CHECK(root);
    CHECK(!root->local.is_valid());
    CHECK(!root->peer->local.is_valid());
    CHECK(root->peer.id() == peer_id);
    CHECK(root->peer.Load().get() == root->alias.Load().get());
    CHECK(root->peer->peer.Load().get() == root);
    CHECK(root->label == "shared root");
    CHECK(root->peer->label == "shared peer");
    BreakCycle(*root);
  }

  ae::RamDomainStorage persisted;
  ae::ObjId receiver_local_id;
  {
    ae::Domain domain{persisted};
    auto root = Descriptor::ptr::Create(ae::CreateWith{domain}.with_id(root_id));
    auto peer = Descriptor::ptr::Create(ae::CreateWith{domain}.with_id(peer_id));
    auto local = ReceiverState::ptr::Create(ae::CreateWith{domain});
    receiver_local_id = local.id();
    local->value = "receiver progress";
    local->pending = {3, 1, 4, 1, 5};
    root->uid = "old root uid";
    root->label = "old root";
    peer->uid = "old peer uid";
    peer->label = "old peer";
    root->local = local;
    peer->local = local;
    root->peer = peer;
    root->alias = peer;
    peer->peer = root;
    peer->alias = root;
    auto* const root_address = &*root;
    auto* const peer_address = &*peer;
    auto* const local_address = &*local;

    // Explicit native hydration reloads known instances. LoadRoot is an
    // identity lookup and intentionally does not reload already-loaded objects.
    ae::DomainGraph graph{&domain,
                          ae::GraphSerializationScope::NetworkShared};
    graph.SetLoadStorage(&incoming);
    graph.retain_loaded_references = true;
    auto const root_loaded = graph.Load(*root, root_id);
    CHECK(root_loaded.has_value());
    CHECK(root_loaded->IsOk());
    auto const peer_loaded = graph.Load(*peer, peer_id);
    CHECK(peer_loaded.has_value());
    CHECK(peer_loaded->IsOk());
    CHECK(&*root == root_address);
    CHECK(&*root->peer == peer_address);
    CHECK(root->peer.Load().get() == root->alias.Load().get());
    CHECK(root->peer->peer.Load().get() == root_address);
    CHECK(root->uid == "root-uid");
    CHECK(peer->uid == "peer-uid");
    CHECK(root->label == "shared root");
    CHECK(peer->label == "shared peer");
    CHECK(root->local.id() == receiver_local_id);
    CHECK(peer->local.id() == receiver_local_id);
    CHECK(&*root->local == local_address);
    CHECK(local->pending == std::vector<std::uint8_t>({3, 1, 4, 1, 5}));
    CHECK(local->value == "receiver progress");
    CHECK(!domain.Find(sender_local_id));
    CHECK(root->local->GetClassId() == ReceiverState::kClassId);

    ae::DomainGraph save{&domain};
    auto const saved = save.SaveRoot(root.Load(), root.id());
    CHECK(saved.IsOk());
    CHECK(persisted.state.count(receiver_local_id) == 1);
    CHECK(persisted.state.count(sender_local_id) == 0);
    BreakCycle(*root);
  }

  // Cold ordinary LocalPersistent loading must restore the receiver's local
  // concrete type and data, including its shared alias across both descriptors.
  {
    ae::Domain domain{persisted};
    ae::DomainGraph graph{&domain};
    auto held = graph.LoadRoot(root_id);
    CHECK(held);
    auto root = held.as<Descriptor>();
    CHECK(root);
    CHECK(root->local.id() == receiver_local_id);
    CHECK(peer_id == root->peer.id());
    CHECK(root->local.Load().get() == root->peer->local.Load().get());
    CHECK(root->local->GetClassId() == ReceiverState::kClassId);
    auto local = root->local.Load().as<ReceiverState>();
    CHECK(local);
    CHECK(local->value == "receiver progress");
    CHECK(local->pending == std::vector<std::uint8_t>({3, 1, 4, 1, 5}));
    CHECK(root->label == "shared root");
    CHECK(root->peer->label == "shared peer");
    CHECK(root->peer->peer.Load().get() == root);
    BreakCycle(*root);
  }
}

}  // namespace
}  // namespace apptraverse::test

int main() {
  apptraverse::EnsureCoreObjectRegistration();
  apptraverse::test::TestNetworkLoadPreservesLocalState();
  std::cout << "network_local_ptr_test OK\n";
}
