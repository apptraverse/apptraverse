#include <cstdlib>
#include <deque>
#include <iostream>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "apptraverse/distill.h"
#include "apptraverse/model_object_proxy.h"
#include "apptraverse/noninteractive_crt.h"
#include "win_ui_command_routing_test_nodes.h"
#include "apptraverse/node.h"
#include "apptraverse/object_serialization.h"
#include "apptraverse/runtime_node.h"
#include "apptraverse/ui/ui_locale_settings.h"
#include "apptraverse/ui/ui_list_container.h"
#include "apptraverse/ui/ui_localization_string.h"
#include "apptraverse/ui/ui_list_container.h"
#include "apptraverse/ui/ui_push_button.h"
#include "apptraverse/ui/ui_string.h"
#include "apptraverse/ui/ui_window.h"

#define CHECK(x)                                                               \
  do {                                                                         \
    if (!(x)) {                                                                \
      std::cerr << "CHECK failed: " #x << '\n';                                \
      std::abort();                                                            \
    }                                                                          \
  } while (false)


#include "apptraverse/platform/windows/win_ui_presenters.h"

namespace apptraverse::test {
namespace {

std::size_t CountJournalEventsWithClass(Node const& node,
                                        std::uint32_t class_id) {
  std::size_t count = 0;
  for (auto const& entry : node.journal) {
    if (entry.event && entry.event->GetClassId() == class_id) {
      ++count;
    }
  }
  return count;
}

void PumpModelWork(std::deque<ModelObjectProxy::ModelWork>& deferred,
                   ae::Domain& model_domain) {
  while (!deferred.empty()) {
    auto work = std::move(deferred.front());
    deferred.pop_front();
    work(model_domain);
  }
}

Node* FindNodeByObjId(ae::Obj& root, ae::ObjId obj_id) {
  std::vector<Node*> nodes;
  CollectReachableNodes(root, nodes);
  for (Node* node : nodes) {
    if (node->obj_id == obj_id) {
      return node;
    }
  }
  return nullptr;
}

ui::UiLocalizationString::ptr MakeSingleLabel(ae::Domain& domain,
                                              char const* utf8) {
  auto label = ui::UiLocalizationString::ptr::Create(ae::CreateWith{domain});
  auto str = ui::UiString::ptr::Create(ae::CreateWith{domain});
  str->utf8 = utf8;
  label->strings = {str};
  return label;
}

void PostDesktopChildCommand(HWND desktop, HWND child, std::uint32_t control_id,
                             std::uint16_t notification_code) {
  LRESULT const result =
      SendMessageW(desktop, WM_COMMAND,
                   MAKEWPARAM(static_cast<WPARAM>(control_id), notification_code),
                   reinterpret_cast<LPARAM>(child));
  CHECK(result == 0);
}

void TestEditRouting() {
  using namespace apptraverse;
  using namespace apptraverse::ui;
  using namespace apptraverse::ui::windows;
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto window = UiDesktopWindow::ptr::Create(ae::CreateWith{domain});
  InitializeRuntimeNode(*window);
  auto desktop = WinDesktopWindowPresenter::ptr::Create(ae::CreateWith{domain});
  desktop->window = window;
  std::size_t commands = 0;
  ModelObjectProxy proxy{[&](ModelObjectProxy::ModelWork work) {
    ++commands;
    work(domain);
  }};
  desktop->model_proxy = &proxy;
  desktop->OnLoad();
  desktop->presentation_loaded = true;
  auto edit = UiEditBox::ptr::Create(ae::CreateWith{domain});
  InitializeRuntimeNode(*edit);
  auto presenter = WinEditBoxPresenter::ptr::Create(ae::CreateWith{domain});
  presenter->edit_box = edit;
  presenter->window_presenter = desktop;
  presenter->layout_x = 10;
  presenter->layout_y = 10;
  presenter->layout_width = 100;
  presenter->layout_height = 24;
  presenter->control_id = 41;
  presenter->model_proxy = &proxy;
  presenter->OnLoad();
  CHECK(presenter->hwnd() != nullptr);
  commands = 0;
  CHECK(SetWindowTextW(presenter->hwnd(), L"route through instance") != 0);
  CHECK(edit->text() == "route through instance");
  CHECK(commands == 1);
  CHECK(!desktop->RouteChildCommand(42, EN_CHANGE, presenter->hwnd()));
  CHECK(commands == 1);
  EnsureExtendedWinUiPresenterRegistration();
  CHECK(SetWindowTextW(presenter->hwnd(), L"second input") != 0);
  CHECK(edit->text() == "second input");
  CHECK(commands == 2);
  presenter->OnUnload();
  desktop->OnUnload();
}

void TestPushButtonNativeClickViaProxyAndPublication() {
  using namespace apptraverse;
  using namespace apptraverse::ui;
  using namespace apptraverse::ui::windows;

  ae::RamDomainStorage model_storage;
  ae::RamDomainStorage ui_storage;
  ae::Domain model_domain{model_storage};
  ae::Domain ui_domain{ui_storage};

  auto button = TestDisableOnClickButton::ptr::Create(
      ae::CreateWith{model_domain});
  auto label = MakeSingleLabel(model_domain, "Click");
  InitializeRuntimeNode(*label, *button);
  button->label = label;
  InitializeRuntimeNode(*button);

  std::deque<ModelObjectProxy::ModelWork> deferred;
  ModelObjectProxy proxy{[&](ModelObjectProxy::ModelWork work) {
    deferred.push_back(std::move(work));
  }};

  ByteSink initial;
  SerializeInitialPublication(*button, initial);
  ByteSource initial_in;
  initial_in.data = initial.bytes.data();
  initial_in.size = initial.bytes.size();
  auto ui_button_obj = LoadInitialPublication(initial_in, ui_domain, ui_storage);
  CHECK(ui_button_obj);
  auto ui_button = TestDisableOnClickButton::ptr::MakeFromThis(
      static_cast<TestDisableOnClickButton*>(ui_button_obj.get()));

  auto ui_locale = UiLocaleSettings::ptr::Create(ae::CreateWith{ui_domain});
  InitializeRuntimeNode(*ui_locale);

  auto ui_desktop_window = UiDesktopWindow::ptr::Create(ae::CreateWith{ui_domain});
  ui_desktop_window->SubmitFrameFromNative(0, 0, 640, 480);
  auto desktop = WinDesktopWindowPresenter::ptr::Create(ae::CreateWith{ui_domain});
  desktop->window = ui_desktop_window;
  desktop->locale = ui_locale;
  desktop->model_proxy = &proxy;
  desktop->OnLoad();
  desktop->presentation_loaded = true;

  constexpr std::uint32_t kButtonId = 9001;
  auto btn_presenter = WinPushButtonPresenter::ptr::Create(ae::CreateWith{ui_domain});
  btn_presenter->button = ui_button;
  btn_presenter->window_presenter = desktop;
  btn_presenter->layout_x = 8;
  btn_presenter->layout_y = 8;
  btn_presenter->layout_width = 100;
  btn_presenter->layout_height = 26;
  btn_presenter->control_id = kButtonId;
  btn_presenter->model_proxy = &proxy;
  btn_presenter->OnLoad();
  HWND const btn_hwnd = btn_presenter->hwnd();
  CHECK(btn_hwnd != nullptr);

  CHECK(deferred.empty());
  CHECK(button->enabled());
  CHECK(ui_button->enabled());
  CHECK(SendMessageW(btn_hwnd, BM_CLICK, 0, 0) != 0);
  CHECK(deferred.size() == 1);
  CHECK(button->enabled());
  CHECK(ui_button->enabled());

  PumpModelWork(deferred, model_domain);
  CHECK(!button->enabled());
  CHECK(ui_button->enabled());
  CHECK(CountJournalEventsWithClass(*button,
                                    SetUiPushButtonEnabledEvent::kClassId) == 1);

}

void TestListNativeSelectionViaProxyAndPublication() {
  using namespace apptraverse;
  using namespace apptraverse::ui;
  using namespace apptraverse::ui::windows;

  ae::RamDomainStorage model_storage;
  ae::RamDomainStorage ui_storage;
  ae::Domain model_domain{model_storage};
  ae::Domain ui_domain{ui_storage};

  auto list = TestActivationList::ptr::Create(ae::CreateWith{model_domain});
  list->selection_index = -1;
  InitializeRuntimeNode(*list);
  auto row0 = UiListRow::ptr::Create(ae::CreateWith{model_domain});
  auto row1 = UiListRow::ptr::Create(ae::CreateWith{model_domain});
  InitializeRuntimeNode(*row0, *list);
  InitializeRuntimeNode(*row1, *list);
  list->rows = {row0, row1};

  std::deque<ModelObjectProxy::ModelWork> deferred;
  ModelObjectProxy proxy{[&](ModelObjectProxy::ModelWork work) {
    deferred.push_back(std::move(work));
  }};

  ByteSink initial;
  SerializeInitialPublication(*list, initial);
  ByteSource initial_in;
  initial_in.data = initial.bytes.data();
  initial_in.size = initial.bytes.size();
  auto ui_list_obj = LoadInitialPublication(initial_in, ui_domain, ui_storage);
  CHECK(ui_list_obj);
  auto ui_list = TestActivationList::ptr::MakeFromThis(
      static_cast<TestActivationList*>(ui_list_obj.get()));
  CHECK(ui_list->selection_index == -1);

  auto ui_desktop_window = UiDesktopWindow::ptr::Create(ae::CreateWith{ui_domain});
  ui_desktop_window->SubmitFrameFromNative(0, 0, 640, 480);
  auto desktop = WinDesktopWindowPresenter::ptr::Create(ae::CreateWith{ui_domain});
  desktop->window = ui_desktop_window;
  desktop->model_proxy = &proxy;
  desktop->OnLoad();
  desktop->presentation_loaded = true;

  constexpr std::uint32_t kListId = 9002;
  auto list_presenter = WinListContainerPresenter::ptr::Create(
      ae::CreateWith{ui_domain});
  list_presenter->container = ui_list;
  list_presenter->window_presenter = desktop;
  list_presenter->layout_x = 8;
  list_presenter->layout_y = 40;
  list_presenter->layout_width = 200;
  list_presenter->layout_height = 120;
  list_presenter->control_id = kListId;
  list_presenter->notify_list_selection_change = true;
  list_presenter->model_proxy = &proxy;
  list_presenter->OnLoad();
  HWND const list_hwnd = list_presenter->hwnd();
  CHECK(list_hwnd != nullptr);
  CHECK(SendMessageW(list_hwnd, LB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(L"Row A")) != LB_ERR);
  CHECK(SendMessageW(list_hwnd, LB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(L"Row B")) != LB_ERR);

  CHECK(SendMessageW(list_hwnd, LB_SETCURSEL, 1, 0) != LB_ERR);
  PostDesktopChildCommand(desktop->hwnd(), list_hwnd, kListId, LBN_SELCHANGE);

  CHECK(deferred.size() == 1);
  CHECK(list->selection_index == -1);
  CHECK(ui_list->selection_index == -1);
  CHECK(CountJournalEventsWithClass(*list, SetUiListSelectionEvent::kClassId) ==
        0);

  PumpModelWork(deferred, model_domain);
  CHECK(list->selection_index == 1);
  CHECK(ui_list->selection_index == -1);
  CHECK(CountJournalEventsWithClass(*list, SetUiListSelectionEvent::kClassId) ==
        1);
  CHECK(ui_list->journal.empty());

  list_presenter->OnUnload();
  desktop->OnUnload();
}

}  // namespace
}  // namespace apptraverse::test

int main() {
  apptraverse::EnableNoninteractiveCrt();
  apptraverse::EnsureObjectRegistration();
  apptraverse::ui::EnsureUiPresenterRegistration();
  apptraverse::ui::windows::EnsureWinUiPresenterRegistration();
  apptraverse::ui::windows::EnsureExtendedWinUiPresenterRegistration();
  apptraverse::test::EnsureCommandRoutingTestNodeRegistration();
  apptraverse::ui::EnsureUiPresenterRegistration();
  apptraverse::test::TestActivationListStructuralMirrorHeadless();
  apptraverse::test::TestEditRouting();
  std::cout << "win_ui_command_routing_test OK\n";
  return 0;
}
