#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <windows.h>
#ifdef RegisterClass
#  undef RegisterClass
#endif
#include <cstdlib>
#include <deque>
#include <iostream>
#include "apptraverse/application_ui_session.h"
#include "apptraverse/distill.h"
#include "apptraverse/model_object_proxy.h"
#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/platform/windows/win_presentation_host.h"
#include "apptraverse/platform/windows/win_ui_environment.h"
#include "apptraverse/platform/windows/win_ui_presenters.h"
#include "apptraverse/platform/windows/win_ui_text_control_presenter.h"
#include "apptraverse/ui/ui_presented_window_composition.h"
using namespace apptraverse;
#define CHECK(c)                                                 \
  do {                                                           \
    if (!(c)) {                                                  \
      std::cerr << "FAILED " #c << " line " << __LINE__ << '\n'; \
      std::abort();                                              \
    }                                                            \
  } while (false)
class TestTextWindow : public NodeFor<TestTextWindow, ui::UiDesktopWindow> {
  APPTRAVERSE_NAMED_OBJECT("test::NativeTextWindow", TestTextWindow,
                           ui::UiDesktopWindow, 0)
 protected:
  TestTextWindow() = default;

 public:
  explicit TestTextWindow(ae::ObjProp p) : NodeFor{p} {}
  std::vector<ui::windows::WinTextControlPresenter::ptr> presenters;
  AE_OBJECT_REFLECT(AE_MMBR(presenters))
  template <class D>
  void Load(ae::Version<0>, D& d) {
    d(base_, presenters);
  }
  template <class D>
  void Save(ae::Version<0>, D& d) const {
    d(base_, presenters);
  }
};
APPTRAVERSE_REGISTER(TestTextWindow);
std::wstring Text(HWND hwnd) {
  int n = GetWindowTextLengthW(hwnd);
  std::wstring s(n + 1, L'\0');
  GetWindowTextW(hwnd, s.data(), n + 1);
  s.resize(n);
  return s;
}
struct AccessibleState {
  std::wstring name;
  long role;
  long state;
  std::wstring value;
};
AccessibleState Accessible(HWND hwnd) {
  IAccessible* object = nullptr;
  CHECK(SUCCEEDED(AccessibleObjectFromWindow(
      hwnd, OBJID_CLIENT, IID_IAccessible, reinterpret_cast<void**>(&object))));
  VARIANT child{};
  child.vt = VT_I4;
  child.lVal = CHILDID_SELF;
  BSTR name = nullptr, value = nullptr;
  VARIANT role{}, state{};
  CHECK(SUCCEEDED(object->get_accName(child, &name)));
  CHECK(SUCCEEDED(object->get_accValue(child, &value)));
  CHECK(SUCCEEDED(object->get_accRole(child, &role)) && role.vt == VT_I4);
  CHECK(SUCCEEDED(object->get_accState(child, &state)) && state.vt == VT_I4);
  AccessibleState result{
      name ? std::wstring{name, SysStringLen(name)} : std::wstring{}, role.lVal,
      state.lVal,
      value ? std::wstring{value, SysStringLen(value)} : std::wstring{}};
  SysFreeString(name);
  SysFreeString(value);
  VariantClear(&role);
  VariantClear(&state);
  object->Release();
  return result;
}
int main() {
  EnableNoninteractiveCrt();
  EnsureObjectRegistration();
  ui::windows::EnsureWinControlPresenterRegistration();
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto root = TestTextWindow::ptr::Create(ae::CreateWith{domain});
  root->BootstrapFrame(60, 60, 600, 400);
  auto window = ui::windows::WinDesktopWindowPresenter::ptr::Create(
      ae::CreateWith{domain});
  auto locale = ui::UiLocaleSettings::ptr::Create(ae::CreateWith{domain});
  locale->BootstrapLanguageIndex(0);
  auto title = ui::UiLocalizationString::ptr::Create(ae::CreateWith{domain});
  auto title_string = ui::UiString::ptr::Create(ae::CreateWith{domain});
  title_string->utf8 = "Native text contract";
  title->strings = {title_string};
  auto definition = ui::UiLayoutDefinition::ptr::Create(ae::CreateWith{domain});
  definition->root.flow = ui::UiLayoutFlow::kVertical;
  definition->root.padding = 8;
  definition->root.gap = 8;
  for (unsigned id : {301u, 302u, 303u, 304u}) {
    ui::UiLayoutNode leaf;
    leaf.control_id = id;
    leaf.min_width = id == 304 ? 0 : 100;
    leaf.min_height = id == 304 ? 0 : (id == 303 ? 100 : 28);
    leaf.grow = id == 303 ? 1 : 0;
    definition->root.children.push_back(leaf);
  }
  auto layout = ui::UiLayoutState::ptr::Create(ae::CreateWith{domain});
  layout->Bootstrap(definition, false, {});
  auto device = ui::UiDeviceState::ptr::Create(ae::CreateWith{domain});
  device->Bootstrap({}, {});
  ui::ComposePresentedWindow(root, window, title, locale, layout, device);
  std::vector<ui::UiTextControl::ptr> controls;
  std::vector<ui::windows::WinTextControlPresenter::ptr> presenters;
  for (unsigned id : {301u, 302u, 303u, 304u, 305u}) {
    auto control = ui::UiTextControl::ptr::Create(ae::CreateWith{domain});
    control->text = id == 304 ? "NativeMeasurementWidthContract label"
                              : (id == 302 ? "readonly" : "");
    control->accessible_label = id == 301   ? "Message"
                                : id == 302 ? "Identity"
                                : id == 303 ? "Messages"
                                : id == 305 ? "Hidden message"
                                            : "";
    control->rows = id == 303 ? std::vector<std::string>{"one", "two"}
                              : std::vector<std::string>{};
    control->kind = id == 303 ? ui::UiTextControlKind::kMultiline
                              : (id == 304 ? ui::UiTextControlKind::kLabel
                                           : ui::UiTextControlKind::kEdit);
    control->read_only = id != 301;
    control->enabled = true;
    control->visible = id != 305;
    control->revision = 0;
    auto presenter = ui::windows::WinTextControlPresenter::ptr::Create(
        ae::CreateWith{domain});
    presenter->control = control;
    presenter->window_presenter = window;
    presenter->control_id = id;
    controls.push_back(control);
    presenters.push_back(presenter);
    root->presenters.push_back(presenter);
  }
  FinalizeDistilledGraph(*root);
  ui::windows::WinUiEnvironment environment{nullptr};
  device->ObserveEnvironment(environment.snapshot(),
                             ui::UiEnvironmentReason::kStartup);
  std::deque<ModelObjectProxy::ModelWork> queued;
  ModelObjectProxy proxy{[&](auto work) { queued.push_back(std::move(work)); }};
  auto drain = [&] {
    while (!queued.empty()) {
      auto work = std::move(queued.front());
      queued.pop_front();
      work(domain);
    }
  };
  auto publish = [&] {
    ByteSink sink;
    SerializeStructuralNodePublication(*root, sink);
    return sink.bytes;
  };
  ui::windows::WinPresentationHost host;
  host.on_desktop_window_user_close = [](void*, ae::Obj*) {};
  {
    ByteSink initial;
    SerializeInitialPublication(*root, initial);
    ApplicationUiSession session{initial.bytes, &host, proxy};
    drain();
    session.ApplyStructural(publish());
    drain();
    auto& mirror = static_cast<TestTextWindow&>(session.root());
    HWND input = mirror.presenters[0]->hwnd(),
         readonly = mirror.presenters[1]->hwnd(),
         history = mirror.presenters[2]->hwnd();
    CHECK(input && readonly && history);
    CHECK(Accessible(input).name == L"Message" &&
          Accessible(input).role == ROLE_SYSTEM_TEXT);
    CHECK(Accessible(readonly).name == L"Identity" &&
          Accessible(readonly).value == L"readonly" &&
          (Accessible(readonly).state & STATE_SYSTEM_READONLY));
    CHECK(Accessible(history).name == L"Messages" &&
          Accessible(history).role == ROLE_SYSTEM_TEXT &&
          Accessible(history).value == L"one\r\ntwo");
    CHECK(mirror.presenters[4]->hwnd() &&
          !IsWindowVisible(mirror.presenters[4]->hwnd()));
    SetForegroundWindow(GetAncestor(input, GA_ROOT));
    SetFocus(input);
    CHECK(GetFocus() == input);
    SendMessageW(input, WM_CHAR, L'a', 0);
    CHECK(queued.size() == 1);
    drain();
    auto stale = publish();
    SendMessageW(input, WM_CHAR, L'b', 0);
    CHECK(Text(input) == L"ab" && queued.size() == 1);
    SendMessageW(input, EM_SETSEL, 1, 1);
    session.ApplyStructural(stale);
    DWORD start{}, end{};
    SendMessageW(input, EM_GETSEL, reinterpret_cast<WPARAM>(&start),
                 reinterpret_cast<LPARAM>(&end));
    CHECK(Text(input) == L"ab" && GetFocus() == input && start == 1 &&
          end == 1);
    CHECK(mirror.presenters[0]->hwnd() == input);
    drain();
    session.ApplyStructural(publish());
    controls[0]->Materialize("", {}, true, true, false);
    auto clear_before_new_typing = publish();
    SendMessageW(input, WM_CHAR, L'c', 0);
    auto newer = Text(input);
    CHECK(newer == L"acb");
    session.ApplyStructural(clear_before_new_typing);
    CHECK(Text(input) == newer && GetFocus() == input);
    drain();
    session.ApplyStructural(publish());
    CHECK(Text(input) == newer);
    controls[0]->Materialize("", {}, true, true, false);
    session.ApplyStructural(publish());
    CHECK(Text(input).empty());
    auto const events = controls[0]->journal.size();
    auto before = queued.size();
    SendMessageW(input, WM_KEYDOWN, VK_RETURN, 0);
    SendMessageW(input, WM_CHAR, VK_RETURN, 0);
    CHECK(queued.size() == before + 1);
    drain();
    CHECK(controls[0]->journal.size() == events + 1);
    CHECK(Text(readonly) == L"readonly");
    SendMessageW(readonly, WM_CHAR, L'x', 0);
    CHECK(Text(readonly) == L"readonly" && queued.empty());
    CHECK(Text(history) == L"one\r\ntwo");
    CHECK((GetWindowLongPtrW(history, GWL_STYLE) &
           (ES_MULTILINE | ES_READONLY)) == (ES_MULTILINE | ES_READONLY));
    auto original_width = root->client_width();
    SetWindowPos(GetAncestor(input, GA_ROOT), nullptr, 60, 60, 150, 240,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    drain();
    CHECK(root->client_width() == original_width);
    HWND label = mirror.presenters[3]->hwnd();
    RECT label_rect{};
    GetWindowRect(label, &label_rect);
    HDC label_dc = GetDC(label);
    auto previous_font = SelectObject(
        label_dc,
        reinterpret_cast<HFONT>(SendMessageW(label, WM_GETFONT, 0, 0)));
    SIZE measured{};
    std::wstring native_word = L"NativeMeasurementWidthContract";
    GetTextExtentPoint32W(label_dc, native_word.data(),
                          static_cast<int>(native_word.size()), &measured);
    SelectObject(label_dc, previous_font);
    ReleaseDC(label, label_dc);
    CHECK(label_rect.right - label_rect.left >= measured.cx);
    device->SetAppearance({ui::UiThemeMode::kDark, true});
    session.ApplyStructural(publish());
    drain();
    CHECK(Text(history) == L"one\r\ntwo" && GetFocus() == input);
    controls[0]->Materialize("", {}, false, true, false);
    session.ApplyStructural(publish());
    CHECK(!IsWindowEnabled(input) &&
          (Accessible(input).state & STATE_SYSTEM_UNAVAILABLE));
    controls[0]->Materialize("", {}, true, true, false);
    session.ApplyStructural(publish());
    auto* presenter_before = mirror.presenters[0].operator->();
    auto order_before = presenter_before->presentation_load_order;
    session.RemountPresentation();
    drain();
    session.ApplyStructural(publish());
    drain();
    CHECK(mirror.presenters[0].operator->() == presenter_before);
    CHECK(presenter_before->presentation_load_order > order_before);
    input = presenter_before->hwnd();
    CHECK(Accessible(input).name == L"Message");
    CHECK(Text(mirror.presenters[2]->hwnd()) == L"one\r\ntwo");
    SetFocus(input);
    SendMessageW(input, WM_CHAR, L'z', 0);
    CHECK(queued.size() == 1);
    drain();
    CHECK(controls[0]->text == "z");
    session.ApplyStructural(publish());
    CHECK(Text(input) == L"z");
  }
  std::cout << "PASS: native text stale ACK, caret/focus, clear/new typing, "
               "Enter once, readonly/multiline, accessibility names/states, "
               "remount, local layout/theme\n";
  return 0;
}
