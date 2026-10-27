#ifndef APPTRAVERSE_TESTS_PRESENTER_PLATFORM_PERSIST_SHARED_H_
#define APPTRAVERSE_TESTS_PRESENTER_PLATFORM_PERSIST_SHARED_H_

#include <cstdint>
#include <filesystem>
#include <iostream>

#include "aether-objects/obj/domain.h"

#include "apptraverse/directory_domain_storage.h"
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

  int backend_tag_on_save;
};

class LoadBackendDesktopWindowPresenter : public ui::UiDesktopWindowPresenter {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::PersistDesktopWindowPresenter",
                           LoadBackendDesktopWindowPresenter,
                           ui::UiDesktopWindowPresenter, 0)

 protected:
  LoadBackendDesktopWindowPresenter() : backend_tag_on_load{2} {}

 public:
  explicit LoadBackendDesktopWindowPresenter(ae::ObjProp prop)
      : ui::UiDesktopWindowPresenter{prop}, backend_tag_on_load{2} {}

  int backend_tag_on_load;
};

inline int RunSaveBackend(std::filesystem::path const& state_dir) {
  APPTRAVERSE_REGISTER(SaveBackendDesktopWindowPresenter);
  EnsureObjectRegistration();
  (void)&g_apptraverse_registrar_PlatformPersistRoot;
  (void)&g_apptraverse_registrar_SaveBackendDesktopWindowPresenter;

  apptraverse::DirectoryDomainStorage storage{state_dir};
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

  storage.BeginSaveSession();
  ae::DomainGraph save_graph{&domain};
  ae::Ptr<ae::Obj> save_root = domain.Find(kRootId);
  if (!save_root || !save_graph.SaveRoot(save_root, kRootId).IsOk() ||
      storage.SaveSessionHadFailure()) {
    std::cerr << "SaveRoot failed\n";
    return 1;
  }
  return 0;
}

inline int RunLoadBackend(std::filesystem::path const& state_dir) {
  APPTRAVERSE_REGISTER(LoadBackendDesktopWindowPresenter);
  EnsureObjectRegistration();
  (void)&g_apptraverse_registrar_PlatformPersistRoot;
  (void)&g_apptraverse_registrar_LoadBackendDesktopWindowPresenter;

  apptraverse::DirectoryDomainStorage storage{state_dir};
  ae::Domain domain{storage};
  ae::DomainGraph load_graph{&domain};
  ae::Ptr<ae::Obj> loaded_obj;
  if (!load_graph.LoadRoot(kRootId, loaded_obj).IsOk()) {
    std::cerr << "LoadRoot failed\n";
    return 1;
  }
  ae::Ptr<PlatformPersistRoot> root{loaded_obj};
  if (root->obj_id != kRootId) {
    std::cerr << "unexpected root id\n";
    return 1;
  }
  if (root->window.id() != kWindowId) {
    std::cerr << "unexpected window id\n";
    return 1;
  }
  if (!root->window_presenter.is_valid() ||
      root->window_presenter.id() != kPresenterId) {
    std::cerr << "unexpected presenter id\n";
    return 1;
  }
  ae::Ptr<ae::Obj> const found_presenter = domain.Find(kPresenterId);
  if (!found_presenter || found_presenter.get() != &*root->window_presenter) {
    std::cerr << "presenter not in load domain\n";
    return 1;
  }

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
