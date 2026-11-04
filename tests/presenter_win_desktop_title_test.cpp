#if !defined(_WIN32)
int main() { return 0; }
#else

#include <iostream>
#include <string>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"

#include "apptraverse/node_for.h"
#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/object_serialization.h"
#include "apptraverse/platform/windows/win_ui_presenters.h"
#include "apptraverse/runtime_node.h"
#include "apptraverse/ui/ui_desktop_window.h"
#include "apptraverse/ui/ui_locale_settings.h"
#include "apptraverse/ui/ui_localization_string.h"
#include "apptraverse/ui/ui_presenters.h"
#include "apptraverse/ui/ui_string.h"

namespace apptraverse::test {
namespace {

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ':'       \
                << __LINE__ << '\n';                                         \
      std::exit(1);                                                          \
    }                                                                        \
  } while (false)

ae::ObjId const kRootId{0x72100000u};
ae::ObjId const kWindowId{0x72100001u};
ae::ObjId const kPresenterId{0x72100002u};
ae::ObjId const kTitleId{0x72100003u};
ae::ObjId const kTitleEnId{0x72100004u};
ae::ObjId const kTitleRuId{0x72100005u};
ae::ObjId const kLocaleId{0x72100006u};
ae::ObjId const kBareRootId{0x72100010u};
ae::ObjId const kBareWindowId{0x72100011u};
ae::ObjId const kBarePresenterId{0x72100012u};

std::wstring const kTitleRu = L"\u0417\u0430\u0433\u043e\u043b\u043e\u0432\u043e\u043a";

class WinTitleRoot : public NodeFor<WinTitleRoot> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::WinTitleRoot", WinTitleRoot,
                           Node, 0)

 protected:
  WinTitleRoot() = default;

 public:
  explicit WinTitleRoot(ae::ObjProp prop) : NodeFor{prop} {}

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

APPTRAVERSE_REGISTER(WinTitleRoot);

std::wstring WindowText(HWND hwnd) {
  wchar_t buffer[256]{};
  int const length = GetWindowTextW(hwnd, buffer, 256);
  return std::wstring(buffer, static_cast<std::size_t>(length));
}

WinTitleRoot::ptr BuildRoot(ae::Domain& domain, ae::ObjId root_id,
                            ae::ObjId window_id, ae::ObjId presenter_id) {
  auto root = WinTitleRoot::ptr::Create(ae::CreateWith{domain}.with_id(root_id));
  InitializeRuntimeNode(*root);
  root->window =
      ui::UiDesktopWindow::ptr::Create(ae::CreateWith{domain}.with_id(window_id));
  InitializeRuntimeNode(*root->window, *root);
  root->window_presenter = ui::windows::WinDesktopWindowPresenter::ptr::Create(
      ae::CreateWith{domain}.with_id(presenter_id));
  root->window_presenter->window = root->window;
  return root;
}

void SaveRoot(ae::Domain& domain, ae::ObjId root_id) {
  ae::DomainGraph save_graph{&domain};
  ae::Ptr<ae::Obj> save_root = domain.Find(root_id);
  CHECK(save_root);
  CHECK(save_graph.SaveRoot(save_root, root_id).IsOk());
}

void ResetTitleAndLocale(ui::windows::WinDesktopWindowPresenter& presenter) {
  presenter.title.Reset();
  presenter.locale.Reset();
  CHECK(presenter.title.is_valid());
  CHECK(!presenter.title.is_loaded());
  CHECK(presenter.title.id() == kTitleId);
  CHECK(presenter.locale.is_valid());
  CHECK(!presenter.locale.is_loaded());
  CHECK(presenter.locale.id() == kLocaleId);
}

void TestUnloadedTitleAndLocaleResolveThroughStorage() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto root = BuildRoot(domain, kRootId, kWindowId, kPresenterId);
  {
    auto title = ui::UiLocalizationString::ptr::Create(
        ae::CreateWith{domain}.with_id(kTitleId));
    auto en = ui::UiString::ptr::Create(ae::CreateWith{domain}.with_id(kTitleEnId));
    en->utf8 = "Title";
    auto ru = ui::UiString::ptr::Create(ae::CreateWith{domain}.with_id(kTitleRuId));
    ru->utf8 = "\xD0\x97\xD0\xB0\xD0\xB3\xD0\xBE\xD0\xBB\xD0\xBE\xD0\xB2\xD0\xBE\xD0\xBA";
    title->strings = {en, ru};
    auto locale = ui::UiLocaleSettings::ptr::Create(
        ae::CreateWith{domain}.with_id(kLocaleId));
    auto language = ui::SetUiLanguageIndexEvent::ptr::Create(ae::CreateWith{domain});
    language->language_index = 1;
    locale->Apply(*language);
    root->window_presenter->title = title;
    root->window_presenter->locale = locale;
  }
  CHECK(root->window_presenter->locale->language_index() == 1);
  SaveRoot(domain, kRootId);

  auto& presenter = *root->window_presenter;

  ResetTitleAndLocale(presenter);
  InitializePresenters(*root, nullptr, nullptr);
  CHECK(presenter.presentation_loaded);
  CHECK(presenter.hwnd() != nullptr);
  CHECK(WindowText(presenter.hwnd()) == kTitleRu);

  SetWindowTextW(presenter.hwnd(), L"");
  ResetTitleAndLocale(presenter);
  presenter.OnModelChanged();
  CHECK(WindowText(presenter.hwnd()) == kTitleRu);

  UnloadPresenters(*root);
  CHECK(presenter.hwnd() == nullptr);
}

void TestAbsentTitleAndLocaleStayEmpty() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto root = BuildRoot(domain, kBareRootId, kBareWindowId, kBarePresenterId);
  auto& presenter = *root->window_presenter;
  CHECK(!presenter.title.is_valid());
  CHECK(!presenter.locale.is_valid());

  InitializePresenters(*root, nullptr, nullptr);
  CHECK(presenter.hwnd() != nullptr);
  CHECK(WindowText(presenter.hwnd()).empty());
  presenter.OnModelChanged();
  CHECK(WindowText(presenter.hwnd()).empty());

  UnloadPresenters(*root);
}

}  // namespace
}  // namespace apptraverse::test

int main() {
  apptraverse::EnableNoninteractiveCrt();
  apptraverse::EnsureObjectRegistration();
  apptraverse::ui::windows::EnsureWinUiPresenterRegistration();
  (void)&apptraverse::test::g_apptraverse_registrar_WinTitleRoot;
  apptraverse::test::TestAbsentTitleAndLocaleStayEmpty();
  apptraverse::test::TestUnloadedTitleAndLocaleResolveThroughStorage();
  std::cout << "presenter_win_desktop_title_test OK\n";
  return 0;
}

#endif
