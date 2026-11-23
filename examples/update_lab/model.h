#pragma once
#include "state_package.h"
#include <functional>
#include "apptraverse/client_local_state.h"
#include "apptraverse/event_for.h"
#include "apptraverse/node_for.h"
#include "apptraverse/object_clone.h"
#ifndef UPDATE_LAB_SCHEMA
#define UPDATE_LAB_SCHEMA 1
#endif
namespace update_lab {
inline constexpr ae::ObjId kUserId{0x7b100001}, kLocalId{0x7b100002},
    kViewId{0x7b100003}, kDownloadId{0x7b100004}, kPrefabId{0x7b100010},
    kSupplyId{0x7b100011};
class LocalizedString : public ae::Obj {
  APPTRAVERSE_NAMED_OBJECT("update_lab::LocalizedString", LocalizedString, ae::Obj, 0)
 protected:
  LocalizedString() = default;
 public:
  explicit LocalizedString(ae::ObjProp p) : Obj{p} {}
  std::string value;
  AE_OBJECT_REFLECT(AE_MMBR(value))
  template<class D> void Load(ae::Version<0>, D& d) { d(base_, value); }
  template<class D> void Save(ae::Version<0>, D& d) const { d(base_, value); }
};
class LocalizationState : public ae::Obj {
  APPTRAVERSE_NAMED_OBJECT("update_lab::LocalizationState", LocalizationState, ae::Obj, 0)
 protected:
  LocalizationState() = default;
 public:
  explicit LocalizationState(ae::ObjProp p) : Obj{p} {}
  std::string locale;
  std::vector<LocalizedString::ptr> strings;
  Bytes fixture_data;
  AE_OBJECT_REFLECT(AE_MMBR(locale), AE_MMBR(strings), AE_MMBR(fixture_data))
  template<class D> void Load(ae::Version<0>, D& d) { d(base_, locale, strings, fixture_data); }
  template<class D> void Save(ae::Version<0>, D& d) const { d(base_, locale, strings, fixture_data); }
};
class TextObject : public ae::Obj {
  APPTRAVERSE_NAMED_OBJECT("update_lab::TextObject", TextObject, ae::Obj, UPDATE_LAB_SCHEMA)
 protected:
  TextObject() = default;
 public:
  explicit TextObject(ae::ObjProp p) : Obj{p} {}
  std::string name;
#if UPDATE_LAB_SCHEMA
  AE_OBJECT_REFLECT(AE_MMBR(name), AE_MMBR(badge))
#else
  AE_OBJECT_REFLECT(AE_MMBR(name))
#endif
  template<class D> void Load(ae::Version<0>, D& d) { d(base_, name); }
  template<class D> void Save(ae::Version<0>, D& d) const { d(base_, name); }
#if UPDATE_LAB_SCHEMA
  std::string badge;
  bool owns_badge{false}; // Runtime ownership, derived from persisted layer presence.
  template<class D> void Load(ae::Version<1>, D& d) { d(base_, badge); }
  template<class D> void Save(ae::Version<1>, D& d) const { d(base_, badge); }
  bool ShouldSaveVersion(ae::Version<1>) const { return !origin_id().is_valid() || owns_badge; }
#endif
};
class SupplyDefinition : public ae::Obj {
  APPTRAVERSE_NAMED_OBJECT("update_lab::SupplyDefinition", SupplyDefinition, ae::Obj, 0)
 protected:
  SupplyDefinition() = default;
 public:
  explicit SupplyDefinition(ae::ObjProp p) : Obj{p} {}
  unsigned release;
  std::vector<std::string> languages;
  AE_OBJECT_REFLECT(AE_MMBR(release), AE_MMBR(languages))
  template<class D> void Load(ae::Version<0>, D& d) { d(base_, release, languages); }
  template<class D> void Save(ae::Version<0>, D& d) const { d(base_, release, languages); }
};
class UserChanged;
class UserState : public apptraverse::NodeFor<UserState> {
  APPTRAVERSE_NAMED_OBJECT("update_lab::UserState", UserState, apptraverse::Node, 0)
 protected:
  UserState() = default;
 public:
  explicit UserState(ae::ObjProp p) : NodeFor{p} {}
  std::string language, note;
  unsigned release;
  std::vector<TextObject::ptr> clones;
  AE_OBJECT_REFLECT(AE_MMBR(language), AE_MMBR(note), AE_MMBR(release), AE_MMBR(clones))
  template<class D> void Load(ae::Version<0>, D& d) { d(base_, language, note, release, clones); }
  template<class D> void Save(ae::Version<0>, D& d) const { d(base_, language, note, release, clones); }
  void Apply(UserChanged const&);
  void Change(unsigned action, std::string text, unsigned value = 0, TextObject::ptr clone = {});
};
class UserChanged : public apptraverse::EventFor<UserState, UserChanged> {
  APPTRAVERSE_NAMED_OBJECT("update_lab::UserChanged", UserChanged, apptraverse::Event, 0)
 protected:
  UserChanged() = default;
 public:
  explicit UserChanged(ae::ObjProp p) : EventFor{p} {}
  unsigned action, value;
  std::string text;
  TextObject::ptr clone;
  AE_OBJECT_REFLECT(AE_MMBR(action), AE_MMBR(value), AE_MMBR(text), AE_MMBR(clone))
  template<class D> void Load(ae::Version<0>, D& d) { d(base_, action, value, text, clone); }
  template<class D> void Save(ae::Version<0>, D& d) const { d(base_, action, value, text, clone); }
};
class LocalChanged;
class DeviceState : public apptraverse::NodeFor<DeviceState, apptraverse::ClientLocalState> {
  APPTRAVERSE_NAMED_OBJECT("update_lab::DeviceState", DeviceState, apptraverse::ClientLocalState, 0)
 protected:
  DeviceState() = default;
 public:
  explicit DeviceState(ae::ObjProp p) : NodeFor{p} {}
  std::string device;
  int x, y, width, height;
  unsigned bytes_per_second;
  AE_OBJECT_REFLECT(AE_MMBR(device), AE_MMBR(x), AE_MMBR(y), AE_MMBR(width), AE_MMBR(height), AE_MMBR(bytes_per_second))
  template<class D> void Load(ae::Version<0>, D& d) { d(base_, device, x, y, width, height, bytes_per_second); }
  template<class D> void Save(ae::Version<0>, D& d) const { d(base_, device, x, y, width, height, bytes_per_second); }
  void Apply(LocalChanged const&);
  void Change(int px, int py, int w, int h, unsigned rate);
};
class LocalChanged : public apptraverse::EventFor<DeviceState, LocalChanged> {
  APPTRAVERSE_NAMED_OBJECT("update_lab::LocalChanged", LocalChanged, apptraverse::Event, 0)
 protected:
  LocalChanged() = default;
 public:
  explicit LocalChanged(ae::ObjProp p) : EventFor{p} {}
  int x, y, width, height;
  unsigned rate;
  AE_OBJECT_REFLECT(AE_MMBR(x), AE_MMBR(y), AE_MMBR(width), AE_MMBR(height), AE_MMBR(rate))
  template<class D> void Load(ae::Version<0>, D& d) { d(base_, x, y, width, height, rate); }
  template<class D> void Save(ae::Version<0>, D& d) const { d(base_, x, y, width, height, rate); }
};
struct FileProgress {
  std::string id;
  std::uint64_t size, received;
  AE_REFLECT_MEMBERS(id, size, received)
};
struct DownloadSnapshot {
  std::uint64_t epoch;
  Intent intent;
  std::string phase, message;
  std::vector<FileProgress> files;
  std::vector<std::string> requested;
  AE_REFLECT_MEMBERS(epoch, intent, phase, message, files, requested)
};
class DownloadChanged;
class DownloadNode : public apptraverse::NodeFor<DownloadNode, apptraverse::ClientLocalState> {
  APPTRAVERSE_NAMED_OBJECT("update_lab::DownloadNode", DownloadNode, apptraverse::ClientLocalState, 0)
 protected:
  DownloadNode() = default;
 public:
  explicit DownloadNode(ae::ObjProp p) : NodeFor{p} {}
  DownloadSnapshot state;
  AE_OBJECT_REFLECT(AE_MMBR(state))
  template<class D> void Load(ae::Version<0>, D& d) { d(base_, state); }
  template<class D> void Save(ae::Version<0>, D& d) const { d(base_, state); }
  void Apply(DownloadChanged const&);
  void Change(DownloadSnapshot snapshot);
};
class DownloadChanged : public apptraverse::EventFor<DownloadNode, DownloadChanged> {
  APPTRAVERSE_NAMED_OBJECT("update_lab::DownloadChanged", DownloadChanged, apptraverse::Event, 0)
 protected:
  DownloadChanged() = default;
 public:
  explicit DownloadChanged(ae::ObjProp p) : EventFor{p} {}
  DownloadSnapshot next;
  AE_OBJECT_REFLECT(AE_MMBR(next))
  template<class D> void Load(ae::Version<0>, D& d) { d(base_, next); }
  template<class D> void Save(ae::Version<0>, D& d) const { d(base_, next); }
};
struct LifecycleOffer {
  unsigned release{kLatest}, executable{};
  std::uint64_t data_bytes{}, download_bytes{};
  bool data_only{}, replace_executable{};
  AE_REFLECT_MEMBERS(release, executable, data_bytes, download_bytes, data_only, replace_executable)
};
struct DistributionView {
  std::vector<unsigned> releases;
  std::string offer;
  std::map<std::string, unsigned> installed;
  LifecycleOffer lifecycle;
  AE_REFLECT_MEMBERS(releases, offer, installed, lifecycle)
};
struct ViewData {
  std::vector<std::string> text, languages, clones;
  std::vector<std::uint32_t> clone_ids;
  std::string language, note, device, detail;
  unsigned release, executable;
  int x, y, width, height;
  unsigned rate;
  bool ready, restarting, commands_done;
  DistributionView distribution;
  AE_REFLECT_MEMBERS(text, languages, clones, clone_ids, language, note, device, detail,
      release, executable, x, y, width, height, rate, ready, restarting, commands_done, distribution)
};
struct Command {
  std::string action, text;
  int a, b, c, d;
  AE_REFLECT_MEMBERS(action, text, a, b, c, d)
};
class LabView : public apptraverse::NodeFor<LabView> {
  APPTRAVERSE_NAMED_OBJECT("update_lab::LabView", LabView, apptraverse::Node, 0)
 protected:
  LabView() = default;
 public:
  explicit LabView(ae::ObjProp p) : NodeFor{p} {}
  ViewData data;
  DownloadNode::ptr download;
  std::function<void(Command)> submit; // Model thread only; never serialized.
  AE_OBJECT_REFLECT(AE_MMBR(data), AE_MMBR(download))
  template<class D> void Load(ae::Version<0>, D& d) { d(base_, data, download); }
  template<class D> void Save(ae::Version<0>, D& d) const { d(base_, data, download); }
  void Submit(Command command) { submit(std::move(command)); }
  void Changed() { NoteMaterializedChange(); }
};
void RegisterModel();
ae::RamDomainStorage BuildSupply(unsigned, bool lifecycle = false); // Publisher only; not linked into consumers.
ae::RamDomainStorage BuildCompleteSupply(unsigned, std::string_view app = {});
LanguagePack BuildLanguage(unsigned, std::string const&, std::string_view app = {});
struct DistilledRelease {
  Release definition;
  ae::RamDomainStorage embedded;
  std::map<std::string, ae::RamDomainStorage> downloads;
};
DistilledRelease DistillRelease(unsigned, std::string_view app = {});
bool ReadLanguage(ae::RamDomainStorage&, Resource const&, LanguagePack&);
TextObject::ptr ClonePrefab(ae::Domain&);
}
