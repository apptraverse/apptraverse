#if !defined(_WIN32)
int main() { return 0; }
#else

#include <iostream>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"

#include "apptraverse/node_for.h"
#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/object_serialization.h"
#include "apptraverse/platform/windows/win_ui_presenters.h"
#include "apptraverse/runtime_node.h"
#include "apptraverse/ui/ui_desktop_window.h"
#include "apptraverse/ui/ui_presenters.h"

namespace apptraverse::test {
namespace {

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << '\n';                           \
      std::exit(1);                                                          \
    }                                                                        \
  } while (false)

ae::ObjId const kRootId{0x72000000u};
ae::ObjId const kWindowId{0x72000001u};
ae::ObjId const kPresenterId{0x72000002u};

class WinPersistRoot : public NodeFor<WinPersistRoot> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::WinPersistRoot", WinPersistRoot,
                           Node, 0)

 protected:
  WinPersistRoot() = default;

 public:
  explicit WinPersistRoot(ae::ObjProp prop) : NodeFor{prop} {}

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
  ui::windows::WinDesktopWindowPresenter::ptr window_presenter;
};

APPTRAVERSE_REGISTER(WinPersistRoot);

void RunWinHwndPersistenceTest() {
  EnableNoninteractiveCrt();
  EnsureObjectRegistration();
  ui::windows::EnsureWinUiPresenterRegistration();
  (void)&g_apptraverse_registrar_WinPersistRoot;

  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto root = WinPersistRoot::ptr::Create(
      ae::CreateWith{domain}.with_id(kRootId));
  InitializeRuntimeNode(*root);
  root->window = ui::UiDesktopWindow::ptr::Create(
      ae::CreateWith{domain}.with_id(kWindowId));
  InitializeRuntimeNode(*root->window, *root);
  root->window_presenter =
      ui::windows::WinDesktopWindowPresenter::ptr::Create(
          ae::CreateWith{domain}.with_id(kPresenterId));
  root->window_presenter->window = root->window;

  ByteSink before;
  SerializePersistentModelSnapshot(*root, before);

  ae::RamDomainStorage load_storage;
  ae::Domain load_domain{load_storage};
  auto load_root = WinPersistRoot::ptr::Create(
      ae::CreateWith{load_domain}.with_id(kRootId));
  InitializeRuntimeNode(*load_root);
  ByteSource in;
  in.data = before.bytes.data();
  in.size = before.bytes.size();
  in.pos = 0;
  LoadPersistentModelSnapshot(in, load_domain, load_storage, *load_root);
  CHECK(load_root->window_presenter.is_valid());
  CHECK(load_root->window_presenter->hwnd() == nullptr);

  InitializePresenters(*load_root, nullptr, nullptr);
  HWND const hwnd = load_root->window_presenter->hwnd();
  CHECK(hwnd != nullptr);

  ByteSink after;
  SerializePersistentModelSnapshot(*load_root, after);
  CHECK(before.bytes == after.bytes);

  InitializePresenters(*load_root, reinterpret_cast<void*>(hwnd), nullptr);
  CHECK(load_root->window_presenter->hwnd() == hwnd);
}

}  // namespace
}  // namespace apptraverse::test

int main() {
  apptraverse::test::RunWinHwndPersistenceTest();
  std::cout << "presenter_win_hwnd_persistence_test OK\n";
  return 0;
}

#endif
