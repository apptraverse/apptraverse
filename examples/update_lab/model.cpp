#include "model.h"
#include "apptraverse/runtime_node.h"
#include "apptraverse/distill.h"
namespace update_lab {
APPTRAVERSE_REGISTER(TextObject);
APPTRAVERSE_REGISTER(LocalizationState);
APPTRAVERSE_REGISTER(SupplyDefinition);
APPTRAVERSE_REGISTER(UserState);
APPTRAVERSE_REGISTER(UserChanged);
APPTRAVERSE_REGISTER(DeviceState);
APPTRAVERSE_REGISTER(LocalChanged);
APPTRAVERSE_REGISTER(DownloadNode);
APPTRAVERSE_REGISTER(DownloadChanged);
APPTRAVERSE_REGISTER(LabView);
void RegisterModel() { apptraverse::EnsureCoreObjectRegistration(); apptraverse::EnsureClientLocalStateRegistration(); }
bool DecodeLanguageDelivery(Bytes const& bytes, LanguagePack& language) {
  StatePackage package; Bytes raw; LanguageDelivery delivery;
  if (!UnpackRaw(bytes, package, raw) || !Decode(raw, delivery)) return false;
  ae::RamDomainStorage storage; unsigned version = 0;
  if (!ApplyStatePackage(delivery.native_image, storage, version)) return false;
  ae::Domain domain{storage};
  auto state = apptraverse::LoadApplication<LocalizationState>(domain, delivery.root);
  language = {state->locale, state->text, {}};
  return true;
}
void UserState::Apply(UserChanged const& e) {
  if (e.action == 0) { language = e.text; release = e.value; }
  if (e.action == 1) note = e.text;
  if (e.action == 2) clones.push_back(e.clone);
  if (e.action == 3 && e.value < clones.size()) clones[e.value].Load()->name = e.text;
#if UPDATE_LAB_SCHEMA
  if (e.action == 4 && e.value < clones.size()) {
    clones[e.value].Load()->badge = e.text;
    clones[e.value].Load()->owns_badge = true;
  }
#endif
  NoteMaterializedChange();
}
void UserState::Change(unsigned action, std::string text, unsigned value, TextObject::ptr clone) {
  auto e = UserChanged::ptr::Create(ae::CreateWith{*domain});
  e->action = action; e->text = std::move(text); e->value = value; e->clone = std::move(clone);
  Commit(std::move(e));
}
void DeviceState::Apply(LocalChanged const& e) {
  x = e.x; y = e.y; width = e.width; height = e.height; bytes_per_second = e.rate;
  NoteMaterializedChange();
}
void DeviceState::Change(int px, int py, int w, int h, unsigned rate) {
  auto e = LocalChanged::ptr::Create(ae::CreateWith{*domain});
  e->SetObjectScope(object_scope());
  e->x = px; e->y = py; e->width = w; e->height = h; e->rate = rate;
  Commit(std::move(e));
}
void DownloadNode::Apply(DownloadChanged const& e) {
  if (e.next.epoch < state.epoch) return;
  state = e.next;
  NoteMaterializedChange();
}
void DownloadNode::Change(DownloadSnapshot snapshot) {
  auto e = DownloadChanged::ptr::Create(ae::CreateWith{*domain});
  e->SetObjectScope(object_scope()); e->next = std::move(snapshot);
  Commit(std::move(e));
}
TextObject::ptr ClonePrefab(ae::Domain& domain) {
  auto source = TextObject::ptr::Declare(ae::CreateWith{domain}.with_id(kPrefabId));
  auto loaded = source.Load();
  auto result = apptraverse::CloneCanonicalRoot(domain, *loaded, domain,
      {.stable_origin = kPrefabId, .stable_prefab_ids = {kPrefabId}});
  if (!result) Fail("prefab clone failed");
  auto clone = TextObject::ptr{result.root};
#if UPDATE_LAB_SCHEMA
  clone.Load()->owns_badge = false;
#endif
  return clone;
}
}
