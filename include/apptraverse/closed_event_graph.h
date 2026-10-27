#ifndef APPTRAVERSE_CLOSED_EVENT_GRAPH_H_
#define APPTRAVERSE_CLOSED_EVENT_GRAPH_H_

#include <cstdint>
#include <initializer_list>
#include <map>
#include <set>
#include <vector>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"
#include "aether-objects/obj/idomain_storage.h"
#include "aether-objects/obj/obj.h"
#include "aether-objects/obj/obj_ptr.h"

#include "apptraverse/event.h"
#include "apptraverse/object_link.h"

namespace apptraverse {

bool SerializeObjectGraph(ae::RamDomainStorage const& storage,
                          std::vector<std::uint8_t>& out);
std::vector<std::uint8_t> SerializeObjectGraph(
    ae::RamDomainStorage const& storage);

bool DeserializeObjectGraph(std::vector<std::uint8_t> const& payload,
                            ae::RamDomainStorage& storage);

struct StoredClassChainInfo {
  ae::ObjId obj_id;
  std::vector<std::uint32_t> chain;
  std::uint32_t most_derived_class_id{0};
};

bool ValidateStoredClassChains(
    ae::RamDomainStorage const& parsed,
    std::vector<StoredClassChainInfo>* out_chains = nullptr);

void CommitObjectGraph(ae::RamDomainStorage const& parsed,
                       ae::IDomainStorage& target_storage);

bool ImportObjectGraphPayload(std::vector<std::uint8_t> const& payload,
                              ae::IDomainStorage& target_storage);

class EventGraphExportBoundary {
 public:
  EventGraphExportBoundary() = default;

  EventGraphExportBoundary(std::initializer_list<ae::ObjId> ids) {
    for (auto id : ids) {
      if (id.is_valid()) {
        permitted_ids_.insert(id);
      }
    }
  }

  void Permit(ae::ObjId id) {
    if (id.is_valid()) {
      permitted_ids_.insert(id);
    }
  }

  void Permit(ae::Obj const& obj) { Permit(obj.obj_id); }

  template <typename T>
  void Permit(ae::ObjPtr<T> const& ptr) {
    if (ptr.is_valid()) {
      Permit(ptr.id());
    }
  }

  template <typename T, LinkScope Scope>
  void Permit(ObjectLink<T, Scope> const& link) {
    if (link.is_valid()) {
      Permit(link.id());
    }
  }

  bool IsPermitted(ae::ObjId id) const {
    return permitted_ids_.find(id) != permitted_ids_.end();
  }

  std::set<ae::ObjId> const& permitted_ids() const {
    return permitted_ids_;
  }

 private:
  std::set<ae::ObjId> permitted_ids_;
};

bool FreezeEventPayload(
    ae::Obj const& event,
    EventGraphExportBoundary const& boundary,
    std::vector<std::uint8_t>& out_payload);

bool FreezeEventPayload(
    ae::Obj const& event,
    std::vector<std::uint8_t>& out_payload);

bool ParseEventPayload(
    std::vector<std::uint8_t> const& payload,
    ae::RamDomainStorage& parsed,
    ae::ObjId& out_root_id);

bool ValidateClosedEventGraphStorage(
    ae::RamDomainStorage const& parsed,
    ae::ObjId root_event_id,
    std::uint32_t expected_event_class_id = 0,
    std::vector<StoredClassChainInfo>* out_chains = nullptr);

ae::Ptr<Event> ImportClosedEventGraph(
    ae::RamDomainStorage const& parsed,
    ae::ObjId root_event_id,
    ae::Domain& receiver_domain,
    ae::IDomainStorage& receiver_storage,
    std::set<ae::ObjId>& reserved_ids,
    std::map<ae::ObjId, ae::ObjId>* out_mapping = nullptr);

bool ValidateStandaloneEventGraph(
    ae::RamDomainStorage const& parsed,
    ae::ObjId root_id,
    std::uint32_t expected_event_class_id);

ae::Ptr<Event> ImportStandaloneEventGraph(
    ae::RamDomainStorage const& parsed,
    ae::ObjId root_id,
    std::uint32_t expected_event_class_id,
    ae::Domain& receiver_domain,
    ae::IDomainStorage& receiver_storage);

}  // namespace apptraverse

#endif  // APPTRAVERSE_CLOSED_EVENT_GRAPH_H_
