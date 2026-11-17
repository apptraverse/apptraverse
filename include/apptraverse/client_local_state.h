#pragma once

#include <set>
#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "apptraverse/node.h"

namespace apptraverse {

// A persisted ownership root whose graph belongs to this installation. The
// marker is a normal serialized class layer, not a path or a native handle.
class ClientLocalState : public Node {
  APPTRAVERSE_OBJECT(ClientLocalState, Node, 0)
 protected:
  ClientLocalState() = default;
 public:
  explicit ClientLocalState(ae::ObjProp prop) : Node{prop} {
    SetObjectScope({obj_id, {}, false});
  }
  AE_OBJECT_REFLECT()
  template <typename Dnv> void Load(ae::Version<0>, Dnv& dnv) { dnv(base_); }
  template <typename Dnv> void Save(ae::Version<0>, Dnv& dnv) const { dnv(base_); }
 protected:
  void CommitLocal(Event::ptr event) {
    event->SetObjectScope(object_scope());
    Commit(std::move(event));
  }
};

std::set<ae::ObjId> CollectClientLocalRoots(ae::RamDomainStorage const& supply);

// Native IDomainStorage routing. All class layers, events and metadata of a
// local ownership graph go to local, with identical object serialization.
class ClientLocalDomainStorage final : public ae::IDomainStorage {
 public:
  ClientLocalDomainStorage(ae::RamDomainStorage& portable,
                          ae::RamDomainStorage& local,
                          ae::IDomainStorage& backing,
                          std::set<ae::ObjId> roots);
  std::unique_ptr<ae::IDomainStorageWriter> Store(ae::DomainQuery const&) override;
  ae::ClassList Enumerate(ae::ObjId const&) override;
  ae::DomainLoad Load(ae::DomainQuery const&) override;
  bool HasVersionAfter(ae::DomainQuery const&) override;
  void Remove(ae::ObjId const&) override;
  void CleanUp() override;
  std::optional<ae::ObjectScope> ReadObjectScope(ae::ObjId const&) const override;
  bool IsConstantObject(ae::ObjId const&) const override;
 private:
  bool IsLocal(ae::ObjId id, std::optional<ae::ObjectScope> scope = {}) const;
  ae::RamDomainStorage& Storage(ae::ObjId id) const;
  ae::RamDomainStorage& portable_;
  ae::RamDomainStorage& local_;
  ae::IDomainStorage& backing_;
  std::set<ae::ObjId> roots_;
};

// Initial transfer may seed LOCAL; a destination with its own local graph
// keeps it in full. Never merge another device's environment into that graph.
void SeedClientLocalStorage(ae::RamDomainStorage& destination,
                           ae::RamDomainStorage const& seed);
void EnsureClientLocalStateRegistration();

}  // namespace apptraverse
