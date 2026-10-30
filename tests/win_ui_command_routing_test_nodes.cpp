#include "win_ui_command_routing_test_nodes.h"

#include <deque>
#include <iostream>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "apptraverse/distill.h"
#include "apptraverse/model_object_proxy.h"
#include "apptraverse/object_serialization.h"
#include "apptraverse/node.h"
#include "apptraverse/runtime_node.h"
#include "apptraverse/ui/ui_locale_settings.h"
#include "apptraverse/ui/ui_localization_string.h"
#include "apptraverse/ui/ui_string.h"
#include "apptraverse/ui/ui_window.h"

namespace apptraverse::test {
namespace {

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

void EnsureCommandRoutingTestNodeRegistration() {
  (void)&g_apptraverse_registrar_TestDisableOnClickButton;
  (void)&g_apptraverse_registrar_TestActivationList;
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
