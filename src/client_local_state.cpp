#include "apptraverse/client_local_state.h"

namespace apptraverse {
APPTRAVERSE_REGISTER(ClientLocalState);

void EnsureClientLocalStateRegistration() {
  (void)&g_apptraverse_registrar_ClientLocalState;
}

std::set<ae::ObjId> CollectClientLocalRoots(ae::RamDomainStorage const& supply) {
  std::set<ae::ObjId> roots;
  for (auto const& [id, layers] : supply.state) {
    auto const scope = supply.ReadObjectScope(id);
    if (layers && layers->contains(ClientLocalState::kClassId) &&
        scope && scope->root_id == id) roots.insert(id);
  }
  return roots;
}

ClientLocalDomainStorage::ClientLocalDomainStorage(
    ae::RamDomainStorage& portable, ae::RamDomainStorage& local,
    ae::IDomainStorage& backing, std::set<ae::ObjId> roots)
    : portable_{portable}, local_{local}, backing_{backing}, roots_{std::move(roots)} {}

bool ClientLocalDomainStorage::IsLocal(
    ae::ObjId id, std::optional<ae::ObjectScope> scope) const {
  if (roots_.contains(id)) return true;
  if (!scope) scope = local_.ReadObjectScope(id);
  if (!scope) scope = portable_.ReadObjectScope(id);
  if (!scope) scope = backing_.ReadObjectScope(id);
  return scope && roots_.contains(scope->root_id);
}
ae::RamDomainStorage& ClientLocalDomainStorage::Storage(ae::ObjId id) const {
  return IsLocal(id) ? local_ : portable_;
}
std::unique_ptr<ae::IDomainStorageWriter> ClientLocalDomainStorage::Store(
    ae::DomainQuery const& query) {
  return (IsLocal(query.id, query.object_scope) ? local_ : portable_).Store(query);
}
ae::ClassList ClientLocalDomainStorage::Enumerate(ae::ObjId const& id) {
  return Storage(id).Enumerate(id);
}
ae::DomainLoad ClientLocalDomainStorage::Load(ae::DomainQuery const& query) {
  return Storage(query.id).Load(query);
}
bool ClientLocalDomainStorage::HasVersionAfter(ae::DomainQuery const& query) {
  return Storage(query.id).HasVersionAfter(query);
}
void ClientLocalDomainStorage::Remove(ae::ObjId const& id) { Storage(id).Remove(id); }
void ClientLocalDomainStorage::CleanUp() { portable_.CleanUp(); local_.CleanUp(); }
std::optional<ae::ObjectScope> ClientLocalDomainStorage::ReadObjectScope(ae::ObjId const& id) const {
  return Storage(id).ReadObjectScope(id);
}
bool ClientLocalDomainStorage::IsConstantObject(ae::ObjId const& id) const {
  return Storage(id).IsConstantObject(id);
}
void SeedClientLocalStorage(ae::RamDomainStorage& destination,
                           ae::RamDomainStorage const& seed) {
  if (!destination.state.empty()) return;
  destination.state = seed.state;
  destination.object_scopes = seed.object_scopes;
}
}  // namespace apptraverse
