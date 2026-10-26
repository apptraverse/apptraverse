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
#include "apptraverse/object_macros.h"
#include "apptraverse/overlay_domain_storage.h"

namespace {

constexpr ae::ObjId::Type kPrefabIdRaw = 81001u;
constexpr ae::ObjId::Type kInstanceIdRaw = 81002u;
constexpr std::uint32_t kPrefabValue = 150u;
constexpr std::uint32_t kMutatedValue = 1601u;

ae::ObjId const kPrefabId{kPrefabIdRaw};
ae::ObjId const kInstanceId{kInstanceIdRaw};

class OriginDiskFacetTargetV0 : public ae::Obj {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::OriginDiskFacetTargetV0",
                           OriginDiskFacetTargetV0, ae::Obj, 0)

 protected:
  OriginDiskFacetTargetV0() = default;

 public:
  explicit OriginDiskFacetTargetV0(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(value))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, value);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, value);
  }

  std::uint32_t value{0};
};

APPTRAVERSE_REGISTER(OriginDiskFacetTargetV0);

void Fail(char const* message) {
  std::cerr << "object_origin_disk_fixture FAIL: " << message << '\n';
  std::exit(1);
}

void EmitOkJson(std::string const& extra_fields = {}) {
  std::cout << "{\"ok\":true";
  if (!extra_fields.empty()) {
    std::cout << ',' << extra_fields;
  }
  std::cout << "}\n";
}

std::filesystem::path StateDir(int argc, char** argv) {
  std::filesystem::path state_dir;
  for (int i = 1; i + 1 < argc; ++i) {
    if (std::string_view{argv[i]} == "--state-dir") {
      state_dir = argv[i + 1];
    }
  }
  if (state_dir.empty()) {
    Fail("missing --state-dir");
  }
  return state_dir;
}

std::string_view Operation(int argc, char** argv) {
  for (int i = 1; i + 1 < argc; ++i) {
    if (std::string_view{argv[i]} == "--op") {
      return argv[i + 1];
    }
  }
  Fail("missing --op");
  return {};
}

std::filesystem::path BackingRoot(std::filesystem::path const& state_dir) {
  return state_dir / "backing";
}

std::filesystem::path UserRoot(std::filesystem::path const& state_dir) {
  return state_dir / "user";
}

void OpInitBacking(std::filesystem::path const& state_dir) {
  std::filesystem::create_directories(BackingRoot(state_dir));
  apptraverse::DirectoryDomainStorage backing{BackingRoot(state_dir)};
  ae::Domain domain{backing};
  OriginDiskFacetTargetV0::ptr prefab = OriginDiskFacetTargetV0::ptr::Create(
      ae::CreateWith{domain}.with_id(kPrefabId));
  prefab.Load()->value = kPrefabValue;
  ae::DomainGraph graph{&domain};
  backing.BeginSaveSession();
  if (!graph.SaveRoot(prefab.Load(), kPrefabId).IsOk()) {
    Fail("SaveRoot P");
  }
  if (backing.SaveSessionHadFailure()) {
    Fail("backing save session failed");
  }
  EmitOkJson();
}

struct DiskOverlayBundle {
  std::unique_ptr<apptraverse::DirectoryDomainStorage> user;
  std::unique_ptr<apptraverse::DirectoryDomainStorage> backing;
  std::unique_ptr<apptraverse::OverlayDomainStorage> overlay;
};

DiskOverlayBundle OpenDiskOverlay(std::filesystem::path const& state_dir) {
  std::filesystem::create_directories(UserRoot(state_dir));
  std::filesystem::create_directories(BackingRoot(state_dir));
  auto user = std::make_unique<apptraverse::DirectoryDomainStorage>(
      UserRoot(state_dir));
  auto backing = std::make_unique<apptraverse::DirectoryDomainStorage>(
      BackingRoot(state_dir));
  auto overlay = std::make_unique<apptraverse::OverlayDomainStorage>(
      *user, *backing, std::set<ae::ObjId>{kPrefabId});
  return DiskOverlayBundle{std::move(user), std::move(backing),
                             std::move(overlay)};
}

void OpCreateInstance(std::filesystem::path const& state_dir) {
  DiskOverlayBundle stores = OpenDiskOverlay(state_dir);
  ae::Domain domain{*stores.overlay};
  ae::DomainGraph graph{&domain};
  ae::Ptr<ae::Obj> prefab_root;
  if (!graph.LoadRoot(kPrefabId, prefab_root).IsOk()) {
    Fail("LoadRoot P");
  }
  ae::Ptr<OriginDiskFacetTargetV0> prefab{prefab_root};
  if (prefab->value != kPrefabValue) {
    Fail("prefab value after load");
  }

  OriginDiskFacetTargetV0::ptr instance = OriginDiskFacetTargetV0::ptr::Create(
      ae::CreateWith{domain}.with_id(kInstanceId));
  ae::Ptr<OriginDiskFacetTargetV0> body = instance.Load();
  body->value = prefab->value;
  body->SetOriginId(kPrefabId);

  stores.user->BeginSaveSession();
  if (!graph.SaveRoot(body, kInstanceId).IsOk()) {
    Fail("SaveRoot A");
  }
  if (stores.user->SaveSessionHadFailure()) {
    Fail("USER save session failed");
  }

  std::cout << "{\"ok\":true"
            << ",\"derived_class_id\":" << OriginDiskFacetTargetV0::kClassId
            << ",\"obj_class_id\":" << ae::Obj::kClassId << "}\n";
}

void OpInspect(std::filesystem::path const& state_dir) {
  DiskOverlayBundle stores = OpenDiskOverlay(state_dir);
  ae::Domain domain{*stores.overlay};
  ae::DomainGraph graph{&domain};
  ae::Ptr<ae::Obj> instance_root;
  if (!graph.LoadRoot(kInstanceId, instance_root).IsOk()) {
    Fail("LoadRoot A");
  }
  ae::Ptr<OriginDiskFacetTargetV0> instance{instance_root};
  if (instance->obj_id != kInstanceId) {
    Fail("instance ObjId");
  }
  if (instance->GetClassId() != OriginDiskFacetTargetV0::kClassId) {
    Fail("instance ClassId");
  }
  if (instance->origin_id() != kPrefabId) {
    Fail("origin_id");
  }
  bool const p_materialized = static_cast<bool>(domain.Find(kPrefabId));
  std::cout << "{\"ok\":true"
            << ",\"instance_id\":" << kInstanceIdRaw
            << ",\"derived_class_id\":" << OriginDiskFacetTargetV0::kClassId
            << ",\"obj_class_id\":" << ae::Obj::kClassId
            << ",\"value\":" << instance->value
            << ",\"origin_id\":" << kPrefabIdRaw
            << ",\"p_materialized\":" << (p_materialized ? "true" : "false")
            << "}\n";
}

void OpMutate(std::filesystem::path const& state_dir) {
  DiskOverlayBundle stores = OpenDiskOverlay(state_dir);
  ae::Domain domain{*stores.overlay};
  ae::DomainGraph load_graph{&domain};
  ae::Ptr<ae::Obj> instance_root;
  if (!load_graph.LoadRoot(kInstanceId, instance_root).IsOk()) {
    Fail("LoadRoot A for mutate");
  }
  ae::Ptr<OriginDiskFacetTargetV0> instance{instance_root};
  instance->value = kMutatedValue;

  stores.user->BeginSaveSession();
  ae::DomainGraph save_graph{&domain};
  if (!save_graph.SaveRoot(instance, kInstanceId).IsOk()) {
    Fail("SaveRoot A after mutate");
  }
  if (stores.user->SaveSessionHadFailure()) {
    Fail("USER save session failed on mutate");
  }
  EmitOkJson();
}

void OpResave(std::filesystem::path const& state_dir) {
  DiskOverlayBundle stores = OpenDiskOverlay(state_dir);
  ae::Domain domain{*stores.overlay};
  ae::DomainGraph load_graph{&domain};
  ae::Ptr<ae::Obj> instance_root;
  if (!load_graph.LoadRoot(kInstanceId, instance_root).IsOk()) {
    Fail("LoadRoot A for resave");
  }
  stores.user->BeginSaveSession();
  ae::DomainGraph save_graph{&domain};
  if (!save_graph.SaveRoot(instance_root, kInstanceId).IsOk()) {
    Fail("SaveRoot A resave");
  }
  if (stores.user->SaveSessionHadFailure()) {
    Fail("USER save session failed on resave");
  }
  EmitOkJson();
}

}  // namespace

int main(int argc, char** argv) {
  apptraverse::EnsureObjectRegistration();
  (void)&g_apptraverse_registrar_OriginDiskFacetTargetV0;

  std::filesystem::path const state_dir = StateDir(argc, argv);
  std::string_view const op = Operation(argc, argv);

  if (op == "init-backing") {
    OpInitBacking(state_dir);
  } else if (op == "create-instance") {
    OpCreateInstance(state_dir);
  } else if (op == "inspect") {
    OpInspect(state_dir);
  } else if (op == "mutate") {
    OpMutate(state_dir);
  } else if (op == "resave") {
    OpResave(state_dir);
  } else {
    Fail("unknown op");
  }
  return 0;
}
