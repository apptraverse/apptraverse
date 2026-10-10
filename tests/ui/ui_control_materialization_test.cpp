#include <iostream>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"

#include "apptraverse/distill.h"
#include "apptraverse/model_object_proxy.h"
#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/runtime_node.h"
#include "apptraverse/ui/ui_edit_box.h"
#include "apptraverse/ui/ui_presenters.h"
#include "apptraverse/ui/ui_push_button.h"
#include "apptraverse/ui/ui_window.h"

using namespace apptraverse;
using namespace apptraverse::ui;

namespace {

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"       \
                << __LINE__ << '\n';                                         \
      std::exit(1);                                                          \
    }                                                                        \
  } while (0)

class TestClickButton : public NodeFor<TestClickButton, UiPushButton> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::TestClickButton", TestClickButton,
                           UiPushButton, 0)

 protected:
  TestClickButton() = default;

 public:
  explicit TestClickButton(ae::ObjProp prop) : NodeFor{prop} {}

  int click_count{0};

  void HandlePushButtonClicked() override { ++click_count; }
};

APPTRAVERSE_REGISTER(TestClickButton);

void EnsureTestRegistration() {
  (void)&g_apptraverse_registrar_TestClickButton;
}

void PendingNotify(void* ctx, Node&) {
  auto* count = static_cast<int*>(ctx);
  ++(*count);
}

void TestEditBoxMaterializationNotifiesOnChange() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto doc = UiWindow::ptr::Create(ae::CreateWith{domain});
  InitializeRuntimeNode(*doc);
  auto edit = UiEditBox::ptr::Create(ae::CreateWith{domain});
  InitializeRuntimeNode(*edit, *doc);

  int notify_count = 0;
  edit->BindMaterializedChangeNotifier(&notify_count, &PendingNotify);

  auto const gen_before = edit->Generation();
  auto text_event = SetUiEditBoxTextEvent::ptr::Create(
      ae::CreateWith{*edit->domain});
  text_event->text = "uid-123";
  edit->Commit(text_event);
  CHECK(notify_count == 1);
  CHECK(edit->Generation() > gen_before);
  CHECK(edit->text() == "uid-123");

  auto caret = SetUiEditBoxCaretEvent::ptr::Create(ae::CreateWith{*edit->domain});
  caret->caret_utf8_offset = 3;
  edit->Commit(caret);
  CHECK(notify_count == 2);

  auto enable = SetUiPushButtonEnabledEvent::ptr::Create(
      ae::CreateWith{domain});
  auto button = TestClickButton::ptr::Create(ae::CreateWith{domain});
  InitializeRuntimeNode(*button, *doc);
  int button_notify = 0;
  button->BindMaterializedChangeNotifier(&button_notify, &PendingNotify);
  enable->enabled = false;
  button->Commit(enable);
  CHECK(button_notify == 1);
  CHECK(!button->enabled());

  auto const gen_after = edit->Generation();
  auto dup = SetUiEditBoxTextEvent::ptr::Create(ae::CreateWith{*edit->domain});
  dup->text = "uid-123";
  edit->Commit(dup);
  CHECK(edit->Generation() == gen_after);
  CHECK(notify_count == 2);

  edit->ClearMaterializedChangeNotifier();
  button->ClearMaterializedChangeNotifier();
}

void TestPushButtonClickViaPresenter() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto doc = UiWindow::ptr::Create(ae::CreateWith{domain});
  InitializeRuntimeNode(*doc);
  auto button = TestClickButton::ptr::Create(ae::CreateWith{domain});
  InitializeRuntimeNode(*button, *doc);

  bool posted = false;
  ModelObjectProxy proxy{[&](ModelObjectProxy::ModelWork work) {
    posted = true;
    work(domain);
  }};

  auto presenter = UiPushButtonPresenter::ptr::Create(ae::CreateWith{domain});
  presenter->button = button;
  presenter->model_proxy = &proxy;
  presenter->control_id = 42;

  presenter->SimulateClickForTest();
  CHECK(posted);
  CHECK(button->click_count == 1);

  button->MaterializeEnabled(false);
  presenter->SimulateClickForTest();
  CHECK(button->click_count == 1);
}

}  // namespace

int main() {
  EnableNoninteractiveCrt();
  EnsureObjectRegistration();
  apptraverse::ui::EnsureUiPresenterRegistration();
  EnsureTestRegistration();

  TestEditBoxMaterializationNotifiesOnChange();
  TestPushButtonClickViaPresenter();
  return 0;
}
