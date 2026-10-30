#include "win_ui_command_routing_test_nodes.h"

#include <deque>
#include <iostream>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "apptraverse/distill.h"
#include "apptraverse/model_object_proxy.h"
#include "apptraverse/object_serialization.h"
#include "apptraverse/node.h"
#include "apptraverse/platform/windows/win_ui_presenters.h"
#include "apptraverse/runtime_node.h"
#include "apptraverse/ui/ui_desktop_window.h"
#include "apptraverse/ui/ui_locale_settings.h"
#include "apptraverse/ui/ui_localization_string.h"
#include "apptraverse/ui/ui_presented_window_composition.h"
#include "apptraverse/ui/ui_string.h"
#include "apptraverse/ui/ui_window.h"

namespace apptraverse::test {
namespace {

ae::ObjId const kButtonRootId{0xE6E60001u};
ae::ObjId const kButtonWindowId{0xE6E60002u};
ae::ObjId const kButtonDesktopPresenterId{0xE6E60003u};
ae::ObjId const kButtonControlId{0xE6E60004u};
ae::ObjId const kButtonPresenterId{0xE6E60005u};
ae::ObjId const kButtonLocaleId{0xE6E60006u};
ae::ObjId const kButtonTitleId{0xE6E60007u};

ae::ObjId const kListRootId{0xE6E60101u};
ae::ObjId const kListWindowId{0xE6E60102u};
ae::ObjId const kListDesktopPresenterId{0xE6E60103u};
ae::ObjId const kListControlId{0xE6E60104u};
ae::ObjId const kListPresenterId{0xE6E60105u};
ae::ObjId const kListLocaleId{0xE6E60106u};
ae::ObjId const kListTitleId{0xE6E60107u};

constexpr std::uint32_t kRoutingButtonControlId = 9001;
constexpr std::uint32_t kRoutingListControlId = 9002;

#define HEADLESS_CHECK(x)                                                      \
  do {                                                                         \
    if (!(x)) {                                                                \
      std::cerr << "HEADLESS_CHECK failed: " #x << '\n';                       \
      std::abort();                                                            \
    }                                                                          \
  } while (0)

ui::UiLocalizationString::ptr MakeSingleLabel(ae::Domain& domain,
                                              char const* utf8) {
  auto label = ui::UiLocalizationString::ptr::Create(ae::CreateWith{domain});
  auto str = ui::UiString::ptr::Create(ae::CreateWith{domain});
  str->utf8 = utf8;
  label->strings = {str};
  return label;
}

void PumpModelWork(std::deque<ModelObjectProxy::ModelWork>& deferred,
                   ae::Domain& model_domain) {
  while (!deferred.empty()) {
    auto work = std::move(deferred.front());
    deferred.pop_front();
    work(model_domain);
  }
}

}  // namespace

void TestDisableOnClickButton::HandlePushButtonClicked() {
  auto event = ui::SetUiPushButtonEnabledEvent::ptr::Create(
      ae::CreateWith{*domain});
  event->enabled = false;
  Commit(std::move(event));
}

void TestActivationList::HandleListRowActivated(std::size_t row_index) {
  last_activated = static_cast<std::int32_t>(row_index);
}

APPTRAVERSE_REGISTER(TestDisableOnClickButton);
APPTRAVERSE_REGISTER(TestActivationList);
APPTRAVERSE_REGISTER(CommandRoutingButtonRoot);
APPTRAVERSE_REGISTER(CommandRoutingListRoot);

ae::ObjId CommandRoutingButtonRootId() { return kButtonRootId; }

ae::ObjId CommandRoutingListRootId() { return kListRootId; }

CommandRoutingButtonRoot::ptr BuildCommandRoutingButtonGraph(
    ae::Domain& domain) {
  auto root = CommandRoutingButtonRoot::ptr::Create(
      ae::CreateWith{domain}.with_id(kButtonRootId));
  InitializeRuntimeNode(*root);

  auto locale = ui::UiLocaleSettings::ptr::Create(
      ae::CreateWith{domain}.with_id(kButtonLocaleId));
  InitializeRuntimeNode(*locale, *root);

  auto title_str = ui::UiString::ptr::Create(ae::CreateWith{domain});
  title_str->utf8 = "Routing button";
  auto title = ui::UiLocalizationString::ptr::Create(
      ae::CreateWith{domain}.with_id(kButtonTitleId));
  title->strings = {title_str};
  InitializeRuntimeNode(*title, *root);

  root->presented_window = ui::UiDesktopWindow::ptr::Create(
      ae::CreateWith{domain}.with_id(kButtonWindowId));
  root->window_presenter =
      ui::windows::WinDesktopWindowPresenter::ptr::Create(
          ae::CreateWith{domain}.with_id(kButtonDesktopPresenterId));
  ui::ComposePresentedWindow(root->presented_window, root->window_presenter,
                             title, locale);
  InitializeRuntimeNode(*root->presented_window, *root);

  root->button = TestDisableOnClickButton::ptr::Create(
      ae::CreateWith{domain}.with_id(kButtonControlId));
  auto label = MakeSingleLabel(domain, "Click");
  InitializeRuntimeNode(*label, *root->button);
  root->button->label = label;
  InitializeRuntimeNode(*root->button, *root->presented_window);

  root->button_presenter = ui::windows::WinPushButtonPresenter::ptr::Create(
      ae::CreateWith{domain}.with_id(kButtonPresenterId));
  root->button_presenter->button = root->button;
  root->button_presenter->window_presenter = root->window_presenter;
  root->button_presenter->layout_x = 8;
  root->button_presenter->layout_y = 8;
  root->button_presenter->layout_width = 100;
  root->button_presenter->layout_height = 26;
  root->button_presenter->control_id = kRoutingButtonControlId;

  auto frame = ui::SetUiDesktopWindowFrameEvent::ptr::Create(
      ae::CreateWith{domain});
  frame->x = 80;
  frame->y = 60;
  frame->client_width = 320;
  frame->client_height = 240;
  root->presented_window->Commit(std::move(frame));

  return root;
}

CommandRoutingListRoot::ptr BuildCommandRoutingListGraph(ae::Domain& domain) {
  auto root = CommandRoutingListRoot::ptr::Create(
      ae::CreateWith{domain}.with_id(kListRootId));
  InitializeRuntimeNode(*root);

  auto locale = ui::UiLocaleSettings::ptr::Create(
      ae::CreateWith{domain}.with_id(kListLocaleId));
  InitializeRuntimeNode(*locale, *root);

  auto title_str = ui::UiString::ptr::Create(ae::CreateWith{domain});
  title_str->utf8 = "Routing list";
  auto title = ui::UiLocalizationString::ptr::Create(
      ae::CreateWith{domain}.with_id(kListTitleId));
  title->strings = {title_str};
  InitializeRuntimeNode(*title, *root);

  root->presented_window = ui::UiDesktopWindow::ptr::Create(
      ae::CreateWith{domain}.with_id(kListWindowId));
  root->window_presenter =
      ui::windows::WinDesktopWindowPresenter::ptr::Create(
          ae::CreateWith{domain}.with_id(kListDesktopPresenterId));
  ui::ComposePresentedWindow(root->presented_window, root->window_presenter,
                             title, locale);
  InitializeRuntimeNode(*root->presented_window, *root);

  root->list = TestActivationList::ptr::Create(
      ae::CreateWith{domain}.with_id(kListControlId));
  root->list->selection_index = -1;
  InitializeRuntimeNode(*root->list, *root->presented_window);
  auto row0 = ui::UiListRow::ptr::Create(ae::CreateWith{domain});
  auto row1 = ui::UiListRow::ptr::Create(ae::CreateWith{domain});
  InitializeRuntimeNode(*row0, *root->list);
  InitializeRuntimeNode(*row1, *root->list);
  row0->primary_text = MakeSingleLabel(domain, "Row A");
  InitializeRuntimeNode(*row0->primary_text, *row0);
  row1->primary_text = MakeSingleLabel(domain, "Row B");
  InitializeRuntimeNode(*row1->primary_text, *row1);
  root->list->rows = {row0, row1};

  root->list_presenter = ui::windows::WinListContainerPresenter::ptr::Create(
      ae::CreateWith{domain}.with_id(kListPresenterId));
  root->list_presenter->container = root->list;
  root->list_presenter->window_presenter = root->window_presenter;
  root->list_presenter->layout_x = 8;
  root->list_presenter->layout_y = 40;
  root->list_presenter->layout_width = 200;
  root->list_presenter->layout_height = 120;
  root->list_presenter->control_id = kRoutingListControlId;
  root->list_presenter->notify_list_selection_change = true;

  auto frame = ui::SetUiDesktopWindowFrameEvent::ptr::Create(
      ae::CreateWith{domain});
  frame->x = 90;
  frame->y = 70;
  frame->client_width = 320;
  frame->client_height = 240;
  root->presented_window->Commit(std::move(frame));

  return root;
}

void EnsureCommandRoutingTestNodeRegistration() {
  (void)&g_apptraverse_registrar_TestDisableOnClickButton;
  (void)&g_apptraverse_registrar_TestActivationList;
  (void)&g_apptraverse_registrar_CommandRoutingButtonRoot;
  (void)&g_apptraverse_registrar_CommandRoutingListRoot;
}

void TestActivationListStructuralMirrorHeadless() {
  ae::RamDomainStorage model_storage;
  ae::RamDomainStorage ui_storage;
  ae::Domain model_domain{model_storage};
  ae::Domain ui_domain{ui_storage};

  auto list = TestActivationList::ptr::Create(ae::CreateWith{model_domain});
  list->selection_index = -1;
  InitializeRuntimeNode(*list);
  auto row0 = ui::UiListRow::ptr::Create(ae::CreateWith{model_domain});
  auto row1 = ui::UiListRow::ptr::Create(ae::CreateWith{model_domain});
  InitializeRuntimeNode(*row0, *list);
  InitializeRuntimeNode(*row1, *list);
  row0->primary_text = MakeSingleLabel(model_domain, "Row A");
  InitializeRuntimeNode(*row0->primary_text, *row0);
  row1->primary_text = MakeSingleLabel(model_domain, "Row B");
  InitializeRuntimeNode(*row1->primary_text, *row1);
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
  auto ui_obj = LoadInitialPublication(initial_in, ui_domain, ui_storage);
  HEADLESS_CHECK(ui_obj);
  auto ui_list = TestActivationList::ptr::MakeFromThis(
      static_cast<TestActivationList*>(ui_obj.get()));

  proxy.Invoke(list->obj_id, &ui::UiListContainer::SubmitListRowSelectionChangedFromUi,
               std::size_t{1});
  PumpModelWork(deferred, model_domain);
  HEADLESS_CHECK(list->selection_index == 1);
  HEADLESS_CHECK(ui_list->selection_index == -1);

  ByteSink structural;
  SerializeStructuralNodePublication(*list, structural);
  ByteSource structural_in;
  structural_in.data = structural.bytes.data();
  structural_in.size = structural.bytes.size();
  ApplyStructuralPublication(structural_in, ui_domain, ui_storage);
  HEADLESS_CHECK(ui_list->selection_index == 1);
}

void TestDisableButtonStructuralMirrorHeadless() {
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
  auto ui_obj = LoadInitialPublication(initial_in, ui_domain, ui_storage);
  HEADLESS_CHECK(ui_obj);
  std::vector<Node*> ui_nodes;
  CollectReachableNodes(*ui_obj, ui_nodes);
  TestDisableOnClickButton* ui_button_raw = nullptr;
  for (Node* node : ui_nodes) {
    if (node->obj_id == button->obj_id) {
      ui_button_raw = static_cast<TestDisableOnClickButton*>(node);
      break;
    }
  }
  HEADLESS_CHECK(ui_button_raw != nullptr);
  auto ui_button = TestDisableOnClickButton::ptr::MakeFromThis(ui_button_raw);

  proxy.Invoke(button->obj_id, &ui::UiPushButton::NotifyClicked);
  HEADLESS_CHECK(deferred.size() == 1);
  PumpModelWork(deferred, model_domain);
  HEADLESS_CHECK(!button->enabled());
  HEADLESS_CHECK(ui_button->enabled());

}

}  // namespace apptraverse::test
