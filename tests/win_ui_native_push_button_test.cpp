#if !defined(_WIN32)
int main() { return 0; }
#else

#include <iostream>
#include <string>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"

#include "apptraverse/model_object_proxy.h"
#include "apptraverse/node_for.h"
#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/object_serialization.h"
#include "apptraverse/platform/windows/win_ui_native_push_button.h"
#include "apptraverse/platform/windows/win_ui_presenters.h"
#include "apptraverse/platform/windows/win_ui_utf8.h"
#include "apptraverse/runtime_node.h"
#include "apptraverse/ui/ui_presenters.h"
#include "apptraverse/ui/ui_desktop_window.h"
#include "apptraverse/ui/ui_localization_string.h"
#include "apptraverse/ui/ui_locale_settings.h"
#include "apptraverse/ui/ui_push_button.h"

namespace apptraverse::test {
namespace {

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"       \
                << __LINE__ << '\n';                                         \
      std::exit(1);                                                          \
    }                                                                        \
  } while (false)

constexpr wchar_t kParentClass[] = L"AppTraverseNativePushButtonTestParent";
constexpr std::uint32_t kControlId = 4242;

class PushButtonRoutingRoot : public NodeFor<PushButtonRoutingRoot> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::PushButtonRoutingRoot",
                           PushButtonRoutingRoot, Node, 0)

 protected:
  PushButtonRoutingRoot() = default;

 public:
  explicit PushButtonRoutingRoot(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(window), AE_MMBR(window_presenter),
                    AE_MMBR(button_presenter))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, window, window_presenter, button_presenter);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, window, window_presenter, button_presenter);
  }

  ui::UiDesktopWindow::ptr window;
  ui::windows::WinDesktopWindowPresenter::ptr window_presenter;
  ui::windows::WinPushButtonPresenter::ptr button_presenter;
};

APPTRAVERSE_REGISTER(PushButtonRoutingRoot);

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

HWND CreateTestParent() {
  static bool registered = false;
  if (!registered) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kParentClass;
    CHECK(RegisterClassW(&wc) != 0);
    registered = true;
  }
  HWND parent = CreateWindowExW(0, kParentClass, L"", WS_OVERLAPPEDWINDOW, 0,
                                0, 320, 240, nullptr, nullptr,
                                GetModuleHandleW(nullptr), nullptr);
  CHECK(parent != nullptr);
  return parent;
}

void TestHelperCreateDestroyRecreate() {
  HWND parent = CreateTestParent();
  HWND btn = ui::windows::CreateChildPushButton({
      .parent_hwnd = parent,
      .control_id = kControlId,
      .initial_caption = L"Add",
      .x = 8,
      .y = 9,
      .width = 100,
      .height = 26,
      .presenter_user_data = reinterpret_cast<void*>(0xABC),
      .create_fatal_operation = "CreateWindowExW Ui push button",
  });
  CHECK(GetDlgCtrlID(btn) == static_cast<int>(kControlId));
  RECT rect{};
  CHECK(GetWindowRect(btn, &rect) != 0);
  CHECK(rect.right - rect.left == 100);
  CHECK(rect.bottom - rect.top == 26);
  wchar_t text[64]{};
  CHECK(GetWindowTextW(btn, text, 64) > 0);
  CHECK(std::wstring{text} == L"Add");

  ui::windows::DestroyChildPushButton(btn, "DestroyWindow Ui push button");
  CHECK(btn == nullptr);

  btn = ui::windows::CreateChildPushButton({
      .parent_hwnd = parent,
      .control_id = kControlId,
      .initial_caption = L"Again",
      .x = 11,
      .y = 21,
      .width = 121,
      .height = 29,
      .presenter_user_data = reinterpret_cast<void*>(0xDEF),
      .create_fatal_operation = "CreateWindowExW Ui push button",
  });
  CHECK(GetWindowTextW(btn, text, 64) > 0);
  CHECK(std::wstring{text} == L"Again");
  ui::windows::DestroyChildPushButton(btn, "DestroyWindow Ui push button");
  ui::windows::DestroyChildPushButton(btn, "DestroyWindow Ui push button");
  CHECK(btn == nullptr);
  DestroyWindow(parent);
}

void TestWinPushButtonPresenterNativeRouting() {
  EnsureObjectRegistration();
  ui::EnsureUiPresenterRegistration();
  ui::windows::EnsureWinUiPresenterRegistration();
  (void)&g_apptraverse_registrar_PushButtonRoutingRoot;

  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto root = PushButtonRoutingRoot::ptr::Create(ae::CreateWith{domain});
  InitializeRuntimeNode(*root);

  auto locale = ui::UiLocaleSettings::ptr::Create(ae::CreateWith{domain});
  InitializeRuntimeNode(*locale);
  auto label = ui::UiLocalizationString::ptr::Create(ae::CreateWith{domain});
  InitializeRuntimeNode(*label, *locale);
  auto en = ui::UiString::ptr::Create(ae::CreateWith{domain});
  en->utf8 = "Add";
  auto ru = ui::UiString::ptr::Create(ae::CreateWith{domain});
  ru->utf8 = "Добавить";
  label->strings = {en, ru};

  root->window = ui::UiDesktopWindow::ptr::Create(ae::CreateWith{domain});
  InitializeRuntimeNode(*root->window, *root);
  root->window->SubmitFrameFromNative(0, 0, 640, 480);

  root->window_presenter =
      ui::windows::WinDesktopWindowPresenter::ptr::Create(
          ae::CreateWith{domain});
  root->window_presenter->window = root->window;
  root->window_presenter->locale = locale;

  auto button = ui::UiPushButton::ptr::Create(ae::CreateWith{domain});
  button->label = label;
  InitializeRuntimeNode(*button, *root->window);

  root->button_presenter = ui::windows::WinPushButtonPresenter::ptr::Create(
      ae::CreateWith{domain});
  root->button_presenter->button = button;
  root->button_presenter->window_presenter = root->window_presenter;
  root->button_presenter->layout_x = 8;
  root->button_presenter->layout_y = 9;
  root->button_presenter->layout_width = 100;
  root->button_presenter->layout_height = 26;
  root->button_presenter->control_id = kControlId;

  int invoke_count = 0;
  ModelObjectProxy proxy{[&](ModelObjectProxy::ModelWork work) {
    ++invoke_count;
    work(domain);
  }};

  InitializePresenters(*root, nullptr, &proxy);
  invoke_count = 0;
  auto* desktop_presenter = root->window_presenter.operator->();
  CHECK(desktop_presenter->hwnd() != nullptr);
  auto* presenter = root->button_presenter.operator->();
  HWND const btn = presenter->hwnd();
  CHECK(btn != nullptr);
  CHECK(GetDlgCtrlID(btn) == static_cast<int>(kControlId));
  CHECK(reinterpret_cast<void*>(GetWindowLongPtrW(btn, GWLP_USERDATA)) ==
        static_cast<void*>(presenter));
  RECT rect{};
  CHECK(GetWindowRect(btn, &rect) != 0);
  CHECK(rect.right - rect.left == 100);
  CHECK(rect.bottom - rect.top == 26);
  wchar_t caption[64]{};
  CHECK(GetWindowTextW(btn, caption, 64) > 0);
  CHECK(std::wstring{caption} == L"Add");
  CHECK(IsWindowEnabled(btn) != FALSE);
  CHECK(presenter->model_proxy == &proxy);
  CHECK(button->enabled());
  LRESULT const click = SendMessageW(
      desktop_presenter->hwnd(), WM_COMMAND,
      MAKEWPARAM(static_cast<WPARAM>(kControlId), BN_CLICKED),
      reinterpret_cast<LPARAM>(btn));
  CHECK(click == 0);
  CHECK(invoke_count == 1);

  SendMessageW(desktop_presenter->hwnd(), WM_COMMAND,
               MAKEWPARAM(static_cast<WPARAM>(kControlId), BN_CLICKED),
               reinterpret_cast<LPARAM>(btn));
  CHECK(invoke_count == 2);

  auto disable = ui::SetUiPushButtonEnabledEvent::ptr::Create(
      ae::CreateWith{domain});
  disable->enabled = false;
  button->Commit(disable);
  presenter->OnModelChanged();
  CHECK(IsWindowEnabled(btn) == FALSE);
  SendMessageW(desktop_presenter->hwnd(), WM_COMMAND,
               MAKEWPARAM(static_cast<WPARAM>(kControlId), BN_CLICKED),
               reinterpret_cast<LPARAM>(btn));
  CHECK(invoke_count == 2);

  auto enable = ui::SetUiPushButtonEnabledEvent::ptr::Create(
      ae::CreateWith{domain});
  enable->enabled = true;
  button->Commit(enable);
  presenter->OnModelChanged();
  CHECK(IsWindowEnabled(btn) != FALSE);

  auto lang = ui::SetUiLanguageIndexEvent::ptr::Create(ae::CreateWith{domain});
  lang->language_index = 1;
  locale->Commit(lang);
  presenter->OnModelChanged();
  CHECK(GetWindowTextW(btn, caption, 64) > 0);
  CHECK(std::wstring{caption} == ui::windows::Utf8ToWide("Добавить"));

  UnloadPresenters(*root);
  CHECK(presenter->hwnd() == nullptr);
  CHECK(desktop_presenter->hwnd() == nullptr);
}

}  // namespace
}  // namespace apptraverse::test

int main() {
  apptraverse::EnableNoninteractiveCrt();
  apptraverse::test::TestHelperCreateDestroyRecreate();
  apptraverse::test::TestWinPushButtonPresenterNativeRouting();
  std::cout << "win_ui_native_push_button_test OK\n";
  return 0;
}

#endif
