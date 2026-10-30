#include <cstdlib>
#include <deque>
#include <iostream>
#include <vector>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "apptraverse/application_ui_session.h"
#include "apptraverse/distill.h"
#include "apptraverse/model_object_proxy.h"
#include "apptraverse/noninteractive_crt.h"
#include "win_ui_command_routing_test_nodes.h"
#include "apptraverse/node.h"
#include "apptraverse/object_serialization.h"
#include "apptraverse/platform/windows/win_ui_frame.h"
#include "apptraverse/runtime_node.h"
#include "apptraverse/ui/ui_desktop_window.h"
#include "apptraverse/ui/ui_list_container.h"
#include "apptraverse/ui/ui_locale_settings.h"
#include "apptraverse/ui/ui_localization_string.h"
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

std::vector<std::uint8_t> SerializeStructuralRoot(Node const& model_root) {
  ByteSink structural;
  SerializeStructuralNodePublication(model_root, structural);
  return structural.bytes;
}

void PumpModelWorkAndApplyStructural(
    std::deque<ModelObjectProxy::ModelWork>& deferred,
    ae::Domain& model_domain, Node const& model_root,
    ApplicationUiSession& session) {
  while (!deferred.empty()) {
    PumpModelWork(deferred, model_domain);
    session.ApplyStructural(SerializeStructuralRoot(model_root));
  }
}

void PostDesktopChildCommand(HWND desktop, HWND child, std::uint32_t control_id,
                             std::uint16_t notification_code) {
  LRESULT const result =
      SendMessageW(desktop, WM_COMMAND,
                   MAKEWPARAM(static_cast<WPARAM>(control_id), notification_code),
                   reinterpret_cast<LPARAM>(child));
  CHECK(result == 0);
}

void MoveHwndClientFrame(HWND hwnd, std::int32_t x, std::int32_t y,
                         std::int32_t client_width,
                         std::int32_t client_height) {
  int outer_w = 0;
  int outer_h = 0;
  ui::windows::ClientSizeToOuter(client_width, client_height, outer_w, outer_h);
  CHECK(SetWindowPos(hwnd, nullptr, x, y, outer_w, outer_h,
                     SWP_NOZORDER | SWP_NOACTIVATE) != 0);
}

CommandRoutingButtonRoot& AsButtonRoot(ae::Obj& root) {
  return static_cast<CommandRoutingButtonRoot&>(root);
}

CommandRoutingListRoot& AsListRoot(ae::Obj& root) {
  return static_cast<CommandRoutingListRoot&>(root);
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
  ae::Domain model_domain{model_storage};

  auto model_root = BuildCommandRoutingButtonGraph(model_domain);
  auto* model_button = model_root->button.operator->();
  auto* model_window = model_root->presented_window.operator->();

  std::deque<ModelObjectProxy::ModelWork> deferred;
  ModelObjectProxy proxy{[&](ModelObjectProxy::ModelWork work) {
    deferred.push_back(std::move(work));
  }};

  ByteSink initial;
  SerializeInitialPublication(*model_root, initial);
  ApplicationUiSession ui_session(initial.bytes, nullptr, proxy);

  auto& ui_root = AsButtonRoot(ui_session.root());
  CHECK(ui_root.domain != model_root->domain);
  auto* desktop = ui_root.window_presenter.operator->();
  auto* btn_presenter = ui_root.button_presenter.operator->();
  auto* ui_button = ui_root.button.operator->();
  CHECK(desktop->hwnd() != nullptr);
  CHECK(desktop->model_proxy == &proxy);
  CHECK(btn_presenter->model_proxy == &proxy);
  HWND const btn_hwnd = btn_presenter->hwnd();
  CHECK(btn_hwnd != nullptr);

  PumpModelWorkAndApplyStructural(deferred, model_domain, *model_root,
                                  ui_session);

  std::int32_t const frame_x_before = model_window->x();
  std::int32_t const frame_y_before = model_window->y();
  std::size_t const native_frame_events_before = CountJournalEventsWithClass(
      *model_window, ReportUiDesktopWindowFrameFromNativeEvent::kClassId);

  MoveHwndClientFrame(desktop->hwnd(), frame_x_before + 40, frame_y_before + 30,
                      model_window->client_width(), model_window->client_height());

  CHECK(model_window->x() == frame_x_before);
  CHECK(model_window->y() == frame_y_before);
  CHECK(ui_root.presented_window->x() == frame_x_before);
  CHECK(ui_root.presented_window->y() == frame_y_before);
  CHECK(!deferred.empty());

  PumpModelWork(deferred, model_domain);
  CHECK(model_window->x() == frame_x_before + 40);
  CHECK(model_window->y() == frame_y_before + 30);
  CHECK(CountJournalEventsWithClass(
            *model_window, ReportUiDesktopWindowFrameFromNativeEvent::kClassId) >
        native_frame_events_before);
  CHECK(ui_root.presented_window->x() == frame_x_before);
  CHECK(ui_root.presented_window->y() == frame_y_before);

  ui_session.ApplyStructural(SerializeStructuralRoot(*model_root));
  CHECK(ui_root.presented_window->x() == model_window->x());
  CHECK(ui_root.presented_window->y() == model_window->y());

  PumpModelWorkAndApplyStructural(deferred, model_domain, *model_root,
                                  ui_session);

  CHECK(deferred.empty());
  CHECK(model_button->enabled());
  CHECK(ui_button->enabled());
  SendMessageW(btn_hwnd, BM_CLICK, 0, 0);
  CHECK(deferred.size() == 1);
  CHECK(model_button->enabled());
  CHECK(ui_button->enabled());

  PumpModelWork(deferred, model_domain);
  CHECK(!model_button->enabled());
  CHECK(ui_button->enabled());
  CHECK(CountJournalEventsWithClass(*model_button,
                                    SetUiPushButtonEnabledEvent::kClassId) == 1);

  ui_session.ApplyStructural(SerializeStructuralRoot(*model_root));
  CHECK(!ui_button->enabled());
  CHECK(IsWindowEnabled(btn_hwnd) == FALSE);
}

void TestListNativeSelectionViaProxyAndPublication() {
  using namespace apptraverse;
  using namespace apptraverse::ui;
  using namespace apptraverse::ui::windows;

  ae::RamDomainStorage model_storage;
  ae::Domain model_domain{model_storage};

  auto model_root = BuildCommandRoutingListGraph(model_domain);
  auto* model_list = model_root->list.operator->();

  std::deque<ModelObjectProxy::ModelWork> deferred;
  ModelObjectProxy proxy{[&](ModelObjectProxy::ModelWork work) {
    deferred.push_back(std::move(work));
  }};

  ByteSink initial;
  SerializeInitialPublication(*model_root, initial);
  ApplicationUiSession ui_session(initial.bytes, nullptr, proxy);

  auto& ui_root = AsListRoot(ui_session.root());
  auto* desktop = ui_root.window_presenter.operator->();
  auto* list_presenter = ui_root.list_presenter.operator->();
  auto* ui_list = ui_root.list.operator->();
  CHECK(desktop->hwnd() != nullptr);
  CHECK(desktop->model_proxy == &proxy);
  CHECK(list_presenter->model_proxy == &proxy);
  HWND const list_hwnd = list_presenter->hwnd();
  CHECK(list_hwnd != nullptr);
  CHECK(SendMessageW(list_hwnd, LB_GETCOUNT, 0, 0) == 2);
  CHECK(ui_list->selection_index == -1);

  PumpModelWorkAndApplyStructural(deferred, model_domain, *model_root,
                                  ui_session);
  CHECK(deferred.empty());

  SendMessageW(list_hwnd, LB_SETCURSEL, 1, 0);
  PostDesktopChildCommand(desktop->hwnd(), list_hwnd, 9002, LBN_SELCHANGE);

  CHECK(deferred.size() == 1);
  CHECK(model_list->selection_index == -1);
  CHECK(ui_list->selection_index == -1);
  CHECK(CountJournalEventsWithClass(*model_list, SetUiListSelectionEvent::kClassId) ==
        0);

  PumpModelWork(deferred, model_domain);
  CHECK(deferred.empty());
  CHECK(model_list->selection_index == 1);
  CHECK(ui_list->selection_index == -1);
  CHECK(CountJournalEventsWithClass(*model_list, SetUiListSelectionEvent::kClassId) ==
        1);

  ui_session.ApplyStructural(SerializeStructuralRoot(*model_root));
  CHECK(ui_list->selection_index == 1);
  CHECK(SendMessageW(list_hwnd, LB_GETCURSEL, 0, 0) == 1);

  PostDesktopChildCommand(desktop->hwnd(), list_hwnd, 9002, LBN_DBLCLK);
  CHECK(deferred.size() == 1);
  CHECK(model_list->last_activated == -1);
  PumpModelWork(deferred, model_domain);
  CHECK(deferred.empty());
  CHECK(model_list->last_activated == 1);
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
  apptraverse::test::TestEditRouting();
  apptraverse::test::TestPushButtonNativeClickViaProxyAndPublication();
  apptraverse::test::TestListNativeSelectionViaProxyAndPublication();
  apptraverse::test::TestActivationListStructuralMirrorHeadless();
  std::cout << "win_ui_command_routing_test OK\n";
  return 0;
}
