#ifndef APPTRAVERSE_TESTS_PRESENTER_PLATFORM_PERSIST_SHARED_H_
#define APPTRAVERSE_TESTS_PRESENTER_PLATFORM_PERSIST_SHARED_H_

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"

#include "apptraverse/node_for.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/object_serialization.h"
#include "apptraverse/runtime_node.h"
#include "apptraverse/ui/ui_desktop_window.h"
#include "apptraverse/ui/ui_presenters.h"

namespace apptraverse::test {
namespace presenter_platform_persist {
namespace {

inline ae::ObjId const kRootId{0x71000000u};
inline ae::ObjId const kWindowId{0x71000001u};
inline ae::ObjId const kPresenterId{0x71000002u};

}  // namespace

class PlatformPersistRoot : public NodeFor<PlatformPersistRoot> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::PlatformPersistRoot",
                           PlatformPersistRoot, Node, 0)

 protected:
  PlatformPersistRoot() = default;

 public:
  explicit PlatformPersistRoot(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(window), AE_MMBR(window_presenter))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, window, window_presenter);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, window, window_presenter);
  }

  ui::UiDesktopWindow::ptr window;
  ae::ObjPtr<ui::UiDesktopWindowPresenter> window_presenter;
};

APPTRAVERSE_REGISTER(PlatformPersistRoot);

// Same serialized ClassId string in both executables; distinct C++ types locally.
class SaveBackendDesktopWindowPresenter : public ui::UiDesktopWindowPresenter {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::PersistDesktopWindowPresenter",
                           SaveBackendDesktopWindowPresenter,
                           ui::UiDesktopWindowPresenter, 0)

 protected:
  SaveBackendDesktopWindowPresenter() = default;

 public:
  explicit SaveBackendDesktopWindowPresenter(ae::ObjProp prop)
      : ui::UiDesktopWindowPresenter{prop} {}

  int backend_tag_on_save{1};
};

class LoadBackendDesktopWindowPresenter : public ui::UiDesktopWindowPresenter {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::PersistDesktopWindowPresenter",
                           LoadBackendDesktopWindowPresenter,
                           ui::UiDesktopWindowPresenter, 0)

 protected:
  LoadBackendDesktopWindowPresenter() = default;

 public:
  explicit LoadBackendDesktopWindowPresenter(ae::ObjProp prop)
      : ui::UiDesktopWindowPresenter{prop} {}

  int backend_tag_on_load{2};
};

inline void WriteSnapshotFile(std::filesystem::path const& path,
                              std::vector<std::uint8_t> const& bytes) {
  std::ofstream out{path, std::ios::binary | std::ios::trunc};
  if (!out) {
    std::cerr << "failed to open snapshot for write: " << path << '\n';
    std::exit(1);
  }
  out.write(reinterpret_cast<char const*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
}

inline std::vector<std::uint8_t> ReadSnapshotFile(
    std::filesystem::path const& path) {
  std::ifstream in{path, std::ios::binary};
  if (!in) {
    std::cerr << "failed to open snapshot for read: " << path << '\n';
    std::exit(1);
  }
  return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in), {});
}

inline int RunSaveBackend(std::filesystem::path const& path) {
  APPTRAVERSE_REGISTER(SaveBackendDesktopWindowPresenter);
  EnsureObjectRegistration();
  (void)&g_apptraverse_registrar_PlatformPersistRoot;
  (void)&g_apptraverse_registrar_SaveBackendDesktopWindowPresenter;

  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto root = PlatformPersistRoot::ptr::Create(
      ae::CreateWith{domain}.with_id(kRootId));
  InitializeRuntimeNode(*root);
  root->window = ui::UiDesktopWindow::ptr::Create(
      ae::CreateWith{domain}.with_id(kWindowId));
  InitializeRuntimeNode(*root->window, *root);
  auto presenter = SaveBackendDesktopWindowPresenter::ptr::Create(
      ae::CreateWith{domain}.with_id(kPresenterId));
  presenter->window = root->window;
  presenter->backend_tag_on_save = 9;
  root->window_presenter = presenter;

  ByteSink snapshot;
  SerializePersistentModelSnapshot(*root, snapshot);
  WriteSnapshotFile(path, snapshot.bytes);
  return 0;
}

inline int RunLoadBackend(std::filesystem::path const& path) {
  APPTRAVERSE_REGISTER(LoadBackendDesktopWindowPresenter);
  EnsureObjectRegistration();
  (void)&g_apptraverse_registrar_PlatformPersistRoot;
  (void)&g_apptraverse_registrar_LoadBackendDesktopWindowPresenter;

  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto root = PlatformPersistRoot::ptr::Create(
      ae::CreateWith{domain}.with_id(kRootId));
  InitializeRuntimeNode(*root);

  auto const bytes = ReadSnapshotFile(path);
  ByteSource in;
  in.data = bytes.data();
  in.size = bytes.size();
  in.pos = 0;
  LoadPersistentModelSnapshot(in, domain, storage, *root);

  auto presenter = LoadBackendDesktopWindowPresenter::ptr::MakeFromThis(
      static_cast<LoadBackendDesktopWindowPresenter*>(&*root->window_presenter));
  if (!presenter.is_valid() || &*presenter->window != &*root->window) {
    std::cerr << "presenter graph not restored\n";
    return 1;
  }
  if (presenter->backend_tag_on_load != 2) {
    std::cerr << "expected load-backend instance\n";
    return 2;
  }
  return 0;
}

}  // namespace presenter_platform_persist
}  // namespace apptraverse::test

#endif
