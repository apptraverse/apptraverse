#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <set>
#include <string>
#include <string_view>

#include "aether-objects/obj/domain.h"
#include "aether-objects/obj/obj.h"

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/event_for.h"
#include "apptraverse/node_for.h"
#include "apptraverse/object_clone.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/object_serialization.h"
#include "apptraverse/overlay_domain_storage.h"

#ifndef APPTRAVERSE_ORIGIN_EVOLUTION_EXE_REVISION
#error APPTRAVERSE_ORIGIN_EVOLUTION_EXE_REVISION is required
#endif
static_assert(APPTRAVERSE_ORIGIN_EVOLUTION_EXE_REVISION == 0 ||
              APPTRAVERSE_ORIGIN_EVOLUTION_EXE_REVISION == 1);

namespace apptraverse::test::origin_evolution {

#if APPTRAVERSE_ORIGIN_EVOLUTION_EXE_REVISION == 0
constexpr unsigned kItemSchema = 0;
#else
constexpr unsigned kItemSchema = 1;
#endif

constexpr ae::ObjId::Type kPrefabIdRaw = 95001u;
constexpr ae::ObjId::Type kInstanceAIdRaw = 95011u;
constexpr ae::ObjId::Type kInstanceBIdRaw = 95012u;
constexpr std::uint32_t kPrefabValue = 150u;
constexpr std::uint32_t kPrefabTag = 252u;

ae::ObjId const kPrefabId{kPrefabIdRaw};
ae::ObjId const kInstanceAId{kInstanceAIdRaw};
ae::ObjId const kInstanceBId{kInstanceBIdRaw};

class EvolutionSetValueEvent;
#if APPTRAVERSE_ORIGIN_EVOLUTION_EXE_REVISION >= 1
class EvolutionSetTagEvent;
#endif

class EvolutionItem : public NodeFor<EvolutionItem> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::origin_evolution::EvolutionItem",
                           EvolutionItem, Node, kItemSchema)

 protected:
  EvolutionItem() = default;

 public:
  explicit EvolutionItem(ae::ObjProp prop) : NodeFor{prop} {}

#if APPTRAVERSE_ORIGIN_EVOLUTION_EXE_REVISION >= 1
  AE_OBJECT_REFLECT(AE_MMBR(value), AE_MMBR(tag), AE_MMBR(own_tag))
#else
  AE_OBJECT_REFLECT(AE_MMBR(value))
#endif

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, value);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, value);
  }

#if APPTRAVERSE_ORIGIN_EVOLUTION_EXE_REVISION >= 1
  template <typename Dnv>
  void Load(ae::Version<1>, Dnv& dnv) {
    dnv(base_, tag, own_tag);
  }

  template <typename Dnv>
  void Save(ae::Version<1>, Dnv& dnv) const {
    dnv(base_, tag, own_tag);
  }

  // Own S1 is written only after the instance changed it; otherwise S1 is
  // the origin's layer.
  bool ShouldSaveVersion(ae::Version<1>) const { return own_tag; }
#endif

  void Apply(EvolutionSetValueEvent const& event);
#if APPTRAVERSE_ORIGIN_EVOLUTION_EXE_REVISION >= 1
  void Apply(EvolutionSetTagEvent const& event);
#endif

  std::uint32_t value{0};
#if APPTRAVERSE_ORIGIN_EVOLUTION_EXE_REVISION >= 1
  std::uint32_t tag{0};
  bool own_tag{false};
#endif
};

class EvolutionSetValueEvent
    : public EventFor<EvolutionItem, EvolutionSetValueEvent> {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::test::origin_evolution::EvolutionSetValueEvent",
      EvolutionSetValueEvent, Event, 0)

 protected:
  EvolutionSetValueEvent() = default;

 public:
  explicit EvolutionSetValueEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(value))

  std::uint32_t value{0};
};

#if APPTRAVERSE_ORIGIN_EVOLUTION_EXE_REVISION >= 1
class EvolutionSetTagEvent
    : public EventFor<EvolutionItem, EvolutionSetTagEvent> {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::test::origin_evolution::EvolutionSetTagEvent",
      EvolutionSetTagEvent, Event, 0)

 protected:
  EvolutionSetTagEvent() = default;

 public:
  explicit EvolutionSetTagEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(tag))

  std::uint32_t tag{0};
};
#endif

void EvolutionItem::Apply(EvolutionSetValueEvent const& event) {
  value = event.value;
}

#if APPTRAVERSE_ORIGIN_EVOLUTION_EXE_REVISION >= 1
void EvolutionItem::Apply(EvolutionSetTagEvent const& event) {
  tag = event.tag;
  own_tag = true;
}
#endif

APPTRAVERSE_REGISTER(EvolutionItem);
APPTRAVERSE_REGISTER(EvolutionSetValueEvent);
#if APPTRAVERSE_ORIGIN_EVOLUTION_EXE_REVISION >= 1
APPTRAVERSE_REGISTER(EvolutionSetTagEvent);
#endif

[[noreturn]] void Fail(std::string_view message) {
  std::cerr << "origin_evolution_fixture FAIL: " << message << '\n';
  std::exit(1);
}

struct Args {
  std::filesystem::path user_dir;
  std::filesystem::path backing_dir;
  std::string op;
  std::string target;
  std::uint32_t number{0};
};

Args ParseArgs(int argc, char** argv) {
  Args args;
  for (int i = 1; i + 1 < argc; i += 2) {
    std::string_view const key{argv[i]};
    char const* value = argv[i + 1];
    if (key == "--user-dir") {
      args.user_dir = value;
    } else if (key == "--backing-dir") {
      args.backing_dir = value;
    } else if (key == "--op") {
      args.op = value;
    } else if (key == "--target") {
      args.target = value;
    } else if (key == "--number") {
      args.number = static_cast<std::uint32_t>(std::stoul(value));
    } else {
      Fail("unknown argument");
    }
  }
  if (args.backing_dir.empty() || args.op.empty()) {
    Fail("usage: --backing-dir DIR [--user-dir DIR] --op OP");
  }
  return args;
}

void OpExportBacking(Args const& args) {
  if (std::filesystem::exists(args.backing_dir) &&
      !std::filesystem::is_empty(args.backing_dir)) {
    Fail("export-backing requires an empty BACKING directory");
  }
  std::filesystem::create_directories(args.backing_dir);
  DirectoryDomainStorage backing{args.backing_dir};
  ae::Domain domain{backing};
  EvolutionItem::ptr prefab =
      EvolutionItem::ptr::Create(ae::CreateWith{domain}.with_id(kPrefabId));
  ae::Ptr<EvolutionItem> body = prefab.Load();
  body->value = kPrefabValue;
#if APPTRAVERSE_ORIGIN_EVOLUTION_EXE_REVISION >= 1
  body->tag = kPrefabTag;
#endif
  ae::DomainGraph graph{&domain};
  backing.BeginSaveSession();
  if (!graph.SaveRoot(body, kPrefabId).IsOk()) {
    Fail("SaveRoot P");
  }
#if APPTRAVERSE_ORIGIN_EVOLUTION_EXE_REVISION >= 1
  if (!graph.SaveVersion(ae::Version<1>{}, *body, kPrefabId).IsOk()) {
    Fail("SaveVersion P/S1");
  }
#endif
  if (backing.SaveSessionHadFailure()) {
    Fail("BACKING save session failed");
  }
  std::cout << "{\"ok\":true,\"op\":\"export-backing\"}\n";
}

struct Stores {
  std::unique_ptr<DirectoryDomainStorage> user;
  std::unique_ptr<DirectoryDomainStorage> backing;
  std::unique_ptr<OverlayDomainStorage> overlay;
};

Stores OpenStores(Args const& args) {
  if (args.user_dir.empty()) {
    Fail("missing --user-dir");
  }
  if (!std::filesystem::is_directory(args.backing_dir)) {
    Fail("BACKING directory does not exist");
  }
  std::filesystem::create_directories(args.user_dir);
  auto user = std::make_unique<DirectoryDomainStorage>(args.user_dir);
  auto backing = std::make_unique<DirectoryDomainStorage>(args.backing_dir);
  auto overlay = std::make_unique<OverlayDomainStorage>(
      *user, *backing, std::set<ae::ObjId>{kPrefabId});
  return Stores{std::move(user), std::move(backing), std::move(overlay)};
}

EvolutionItem::ptr LoadItem(ae::Domain& domain, ae::ObjId id) {
  auto item = EvolutionItem::ptr::Declare(ae::CreateWith{domain}.with_id(id));
  item.Load();
  if (!item.is_loaded()) {
    Fail("instance Load failed");
  }
  return item;
}

void SaveItems(Stores& stores, EvolutionItem::ptr const& a,
               EvolutionItem::ptr const& b) {
  stores.user->BeginSaveSession();
  a.Save();
  b.Save();
  if (stores.user->SaveSessionHadFailure()) {
    Fail("USER save session failed");
  }
}

void WriteItem(std::ostream& out, char const* name,
               EvolutionItem::ptr const& item) {
  auto base = EvolutionItem::ptr{item->base};
  if (base.is_valid() && !base.is_loaded()) {
    base.Load();
  }
  out << ",\"" << name << "\":{\"value\":" << item->value
#if APPTRAVERSE_ORIGIN_EVOLUTION_EXE_REVISION >= 1
      << ",\"tag\":" << item->tag
      << ",\"own_tag\":" << (item->own_tag ? "true" : "false")
#endif
      << ",\"journal_len\":" << item->journal.size()
      << ",\"origin_id\":" << item->origin_id().id()
      << ",\"base_id\":" << (base.is_loaded() ? base.id().id() : 0u);
  if (base.is_loaded()) {
    out << ",\"base_value\":" << base->value
        << ",\"base_origin_id\":" << base->origin_id().id();
#if APPTRAVERSE_ORIGIN_EVOLUTION_EXE_REVISION >= 1
    out << ",\"base_tag\":" << base->tag;
#endif
  }
  out << '}';
}

void WriteState(std::string_view op, ae::Domain& domain,
                EvolutionItem::ptr const& a, EvolutionItem::ptr const& b) {
  std::cout << "{\"ok\":true,\"op\":\"" << op << "\",\"revision\":"
            << APPTRAVERSE_ORIGIN_EVOLUTION_EXE_REVISION
            << ",\"prefab_id\":" << kPrefabIdRaw
            << ",\"item_class_id\":" << EvolutionItem::kClassId
            << ",\"obj_class_id\":" << ae::Obj::kClassId;
  WriteItem(std::cout, "a", a);
  WriteItem(std::cout, "b", b);
  std::cout << ",\"p_materialized\":"
            << (domain.Find(kPrefabId) ? "true" : "false") << "}\n";
}

void OpCreateInstances(Args const& args) {
  Stores stores = OpenStores(args);
  ae::Domain domain{*stores.overlay};
  ae::DomainGraph graph{&domain};
  ae::Ptr<ae::Obj> prefab_root;
  if (!graph.LoadRoot(kPrefabId, prefab_root).IsOk() || !prefab_root) {
    Fail("LoadRoot P");
  }
  for (ae::ObjId const id : {kInstanceAId, kInstanceBId}) {
    CloneOptions options{};
    options.stable_origin = kPrefabId;
    options.boundary_ids = {kPrefabId};
    options.new_root_id = id;
    stores.user->BeginSaveSession();
    CloneCanonicalResult const cloned =
        CloneCanonicalRoot(domain, *prefab_root, domain, options);
    if (!cloned || cloned.new_root_id() != id) {
      Fail("CloneCanonicalRoot");
    }
    if (stores.user->SaveSessionHadFailure()) {
      Fail("USER save session failed after clone");
    }
  }

  ae::Domain verify_domain{*stores.overlay};
  EvolutionItem::ptr a = LoadItem(verify_domain, kInstanceAId);
  EvolutionItem::ptr b = LoadItem(verify_domain, kInstanceBId);
  WriteState("create-instances", verify_domain, a, b);
}

void OpInspect(Args const& args) {
  Stores stores = OpenStores(args);
  ae::Domain domain{*stores.overlay};
  EvolutionItem::ptr a = LoadItem(domain, kInstanceAId);
  EvolutionItem::ptr b = LoadItem(domain, kInstanceBId);
  WriteState(args.op, domain, a, b);
}

void OpResave(Args const& args) {
  Stores stores = OpenStores(args);
  ae::Domain domain{*stores.overlay};
  EvolutionItem::ptr a = LoadItem(domain, kInstanceAId);
  EvolutionItem::ptr b = LoadItem(domain, kInstanceBId);
  SaveItems(stores, a, b);
  WriteState(args.op, domain, a, b);
}

#if APPTRAVERSE_ORIGIN_EVOLUTION_EXE_REVISION >= 1

EvolutionItem::ptr UiItem(ae::Domain& ui_domain, ae::ObjId id) {
  ae::Ptr<ae::Obj> object = ui_domain.Find(id);
  if (!object || object->GetClassId() != EvolutionItem::kClassId) {
    Fail("UI mirror missing EvolutionItem");
  }
  return EvolutionItem::ptr::MakeFromThis(
      static_cast<EvolutionItem*>(&*object));
}

ae::Ptr<ae::Obj> PublishInitialToUi(EvolutionItem& model, ae::Domain& ui_domain,
                                    ae::RamDomainStorage& ui_storage) {
  model.EnsureCurrentGeneration();
  ByteSink out;
  SerializeInitialPublication(model, out);
  ByteSource in;
  in.data = out.bytes.data();
  in.size = out.bytes.size();
  in.pos = 0;
  return LoadInitialPublication(in, ui_domain, ui_storage);
}

void PublishStructuralToUi(EvolutionItem& model, ae::Domain& ui_domain,
                           ae::RamDomainStorage& ui_storage) {
  model.EnsureCurrentGeneration();
  ByteSink out;
  SerializeStructuralNodePublication(model, out);
  ByteSource in;
  in.data = out.bytes.data();
  in.size = out.bytes.size();
  in.pos = 0;
  ApplyStructuralPublication(in, ui_domain, ui_storage);
}

void WriteReplayItem(std::ostream& out, char const* name,
                     EvolutionItem::ptr const& item) {
  auto base = EvolutionItem::ptr{item->base};
  if (!base.is_valid() || !base.is_loaded()) {
    base.Load();
  }
  std::uint32_t const model_tag_before = item->tag;
  std::uint32_t const base_tag_before = base->tag;
  bool const replay_ok = item->TryReplayFromBase();
  out << ",\"" << name << "\":{\"model_tag_before\":" << model_tag_before
      << ",\"base_tag_before\":" << base_tag_before
      << ",\"replay_ok\":" << (replay_ok ? "true" : "false")
      << ",\"model_tag_after\":" << item->tag
      << ",\"model_value_after\":" << item->value << '}';
}

void OpReplayBase(Args const& args) {
  Stores stores = OpenStores(args);
  ae::Domain domain{*stores.overlay};
  EvolutionItem::ptr a = LoadItem(domain, kInstanceAId);
  EvolutionItem::ptr b = LoadItem(domain, kInstanceBId);
  std::cout << "{\"ok\":true,\"op\":\"replay-base\",\"revision\":"
            << APPTRAVERSE_ORIGIN_EVOLUTION_EXE_REVISION;
  WriteReplayItem(std::cout, "a", a);
  WriteReplayItem(std::cout, "b", b);
  std::cout << "}\n";
}

void OpCompactJournal(Args const& args) {
  Stores stores = OpenStores(args);
  ae::Domain domain{*stores.overlay};
  EvolutionItem::ptr a = LoadItem(domain, kInstanceAId);
  EvolutionItem::ptr b = LoadItem(domain, kInstanceBId);
  std::uint64_t const now_us = SystemUtcMicros();
  a->CompactJournal(now_us);
  b->CompactJournal(now_us);
  SaveItems(stores, a, b);
  WriteState("compact-journal", domain, a, b);
}

void OpPublishUi(Args const& args) {
  Stores stores = OpenStores(args);
  ae::Domain model_domain{*stores.overlay};
  EvolutionItem::ptr a = LoadItem(model_domain, kInstanceAId);
  EvolutionItem::ptr b = LoadItem(model_domain, kInstanceBId);

  std::uint32_t const model_a_tag = a->tag;
  std::uint32_t const model_b_tag = b->tag;
  std::uint32_t const model_a_value_before = a->value;

  ae::RamDomainStorage ui_storage;
  ae::Domain ui_domain{ui_storage};

  ae::Ptr<ae::Obj> ui_a_obj = PublishInitialToUi(*a, ui_domain, ui_storage);
  ae::Ptr<ae::Obj> ui_b_obj = PublishInitialToUi(*b, ui_domain, ui_storage);
  if (!ui_a_obj || ui_a_obj->obj_id != kInstanceAId ||
      ui_a_obj->GetClassId() != EvolutionItem::kClassId) {
    Fail("initial publication A");
  }
  if (!ui_b_obj || ui_b_obj->obj_id != kInstanceBId ||
      ui_b_obj->GetClassId() != EvolutionItem::kClassId) {
    Fail("initial publication B");
  }
  EvolutionItem::ptr ui_a = EvolutionItem::ptr::MakeFromThis(
      static_cast<EvolutionItem*>(&*ui_a_obj));
  EvolutionItem::ptr ui_b = EvolutionItem::ptr::MakeFromThis(
      static_cast<EvolutionItem*>(&*ui_b_obj));
  std::uint32_t const mirror_a_tag_initial = ui_a->tag;
  std::uint32_t const mirror_b_tag_initial = ui_b->tag;
  std::uint32_t const mirror_a_value_initial = ui_a->value;
  std::uint32_t const mirror_b_value_initial = ui_b->value;

  std::uint32_t const next_value =
      args.number != 0 ? args.number : (model_a_value_before + 100u);
  auto value_event =
      EvolutionSetValueEvent::ptr::Create(ae::CreateWith{model_domain});
  value_event->value = next_value;
  a->Commit(value_event);

  PublishStructuralToUi(*a, ui_domain, ui_storage);
  PublishStructuralToUi(*b, ui_domain, ui_storage);

  ui_a = UiItem(ui_domain, kInstanceAId);
  ui_b = UiItem(ui_domain, kInstanceBId);

  std::cout << "{\"ok\":true,\"op\":\"publish-ui\",\"revision\":"
            << APPTRAVERSE_ORIGIN_EVOLUTION_EXE_REVISION
            << ",\"model_a_tag\":" << model_a_tag
            << ",\"model_b_tag\":" << model_b_tag
            << ",\"mirror_a_tag_initial\":" << mirror_a_tag_initial
            << ",\"mirror_b_tag_initial\":" << mirror_b_tag_initial
            << ",\"mirror_a_tag_structural\":" << ui_a->tag
            << ",\"mirror_b_tag_structural\":" << ui_b->tag
            << ",\"model_a_value_after_event\":" << a->value
            << ",\"mirror_a_value_initial\":" << mirror_a_value_initial
            << ",\"mirror_b_value_initial\":" << mirror_b_value_initial
            << ",\"mirror_a_value_structural\":" << ui_a->value
            << ",\"mirror_b_value_structural\":" << ui_b->value << "}\n";
}

#endif

ae::ObjId TargetId(Args const& args) {
  if (args.target == "a") {
    return kInstanceAId;
  }
  if (args.target == "b") {
    return kInstanceBId;
  }
  Fail("--target must be a or b");
}

template <typename EventT, typename Fill>
void CommitEvent(Args const& args, Fill&& fill) {
  Stores stores = OpenStores(args);
  ae::Domain domain{*stores.overlay};
  EvolutionItem::ptr a = LoadItem(domain, kInstanceAId);
  EvolutionItem::ptr b = LoadItem(domain, kInstanceBId);
  EvolutionItem::ptr const& target = TargetId(args) == kInstanceAId ? a : b;
  auto event = EventT::ptr::Create(ae::CreateWith{domain});
  fill(*event);
  target->Commit(event);
  SaveItems(stores, a, b);
  WriteState(args.op, domain, a, b);
}

int Run(int argc, char** argv) {
  EnsureObjectRegistration();
  Args const args = ParseArgs(argc, argv);
  if (args.op == "export-backing") {
    OpExportBacking(args);
  } else if (args.op == "create-instances") {
    OpCreateInstances(args);
  } else if (args.op == "inspect") {
    OpInspect(args);
  } else if (args.op == "resave") {
    OpResave(args);
  } else if (args.op == "set-value") {
    CommitEvent<EvolutionSetValueEvent>(
        args, [&](EvolutionSetValueEvent& e) { e.value = args.number; });
#if APPTRAVERSE_ORIGIN_EVOLUTION_EXE_REVISION >= 1
  } else if (args.op == "set-tag") {
    CommitEvent<EvolutionSetTagEvent>(
        args, [&](EvolutionSetTagEvent& e) { e.tag = args.number; });
  } else if (args.op == "publish-ui") {
    OpPublishUi(args);
  } else if (args.op == "replay-base") {
    OpReplayBase(args);
  } else if (args.op == "compact-journal") {
    OpCompactJournal(args);
#endif
  } else {
    Fail("unknown --op");
  }
  return 0;
}

}  // namespace apptraverse::test::origin_evolution

int main(int argc, char** argv) {
  return apptraverse::test::origin_evolution::Run(argc, argv);
}
