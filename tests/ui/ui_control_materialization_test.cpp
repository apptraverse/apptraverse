#include <iostream>
#include <vector>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"

#include "apptraverse/distill.h"
#include "apptraverse/model_object_proxy.h"
#include "apptraverse/object_serialization.h"
#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/runtime_node.h"
#include "apptraverse/ui/ui_edit_box.h"
#include "apptraverse/ui/ui_localization_string.h"
#include "apptraverse/ui/ui_push_button_presenter.h"
#include "apptraverse/ui/ui_presenters.h"
#include "apptraverse/ui/ui_push_button.h"
#include "apptraverse/ui/ui_string.h"
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

void TestLocalizationLazyLoadRoundtrip() {
  ae::RamDomainStorage storage;
  ae::ObjId const root_id{1};
  ae::ObjId en_string_id;
  ae::ObjId ru_string_id;
  char const* const kEnText = "Lazy EN slot";
  char const* const kRuText = "Lazy RU slot";
  char const* const kRuUpdated = "Updated lazy RU";

  {
    ae::Domain domain{storage};
    auto localized = ui::UiLocalizationString::ptr::Create(
        ae::CreateWith{domain}.with_id(root_id));
    InitializeRuntimeNode(*localized);

    auto en = ui::UiString::ptr::Create(ae::CreateWith{domain});
    en->utf8 = kEnText;
    auto ru = ui::UiString::ptr::Create(ae::CreateWith{domain});
    ru->utf8 = kRuText;
    en_string_id = en->obj_id;
    ru_string_id = ru->obj_id;
    en.Save();
    ru.Save();

    localized->strings = {
        ui::UiString::ptr::Declare(
            ae::CreateWith{domain}
                .with_id(en_string_id)
                .with_flags(ae::ObjFlags::kUnloadedByDefault)),
        ui::UiString::ptr::Declare(
            ae::CreateWith{domain}
                .with_id(ru_string_id)
                .with_flags(ae::ObjFlags::kUnloadedByDefault)),
    };
    ui::UiLocalizationString::ptr::MakeFromThis(&*localized).Save();
  }

  ae::Domain domain{storage};
  auto loaded = ui::UiLocalizationString::ptr::Declare(
      ae::CreateWith{domain}.with_id(root_id));
  loaded.Load();
  CHECK(loaded->strings.size() == 2);
  CHECK(loaded->strings[0].id() == en_string_id);
  CHECK(loaded->strings[1].id() == ru_string_id);
  CHECK(!loaded->strings[0].is_loaded());
  CHECK(!loaded->strings[1].is_loaded());

  {
    auto en_view = loaded->strings[0].Load();
    CHECK(en_view->utf8 == kEnText);
  }
  CHECK(!loaded->strings[1].is_loaded());

  {
    auto edit = SetUiLocalizationSlotUtf8Event::ptr::Create(
        ae::CreateWith{domain});
    edit->slot_index = 1;
    edit->utf8 = kRuUpdated;
    loaded->Commit(std::move(edit));
  }
  CHECK(loaded->strings[0].is_loaded());
  CHECK(!loaded->strings[1].is_loaded());
  CHECK(loaded->strings[0].id() == en_string_id);
  CHECK(loaded->strings[1].id() == ru_string_id);
  CHECK((loaded->strings[1].flags() & ae::ObjFlags::kUnloadedByDefault) != 0);
  CHECK(!domain.Find(ru_string_id));

  loaded->strings[0].Save();
  loaded->strings[0].Reset();
  CHECK(loaded->strings[0].id() == en_string_id);
  CHECK(!loaded->strings[0].is_loaded());
  CHECK((loaded->strings[0].flags() & ae::ObjFlags::kUnloadedByDefault) != 0);
  CHECK((loaded->strings[0].flags() & ae::ObjFlags::kUnloaded) != 0);

  {
    auto en_view = loaded->strings[0].Load();
    CHECK(en_view->utf8 == kEnText);
  }
  CHECK((loaded->strings[0].flags() & ae::ObjFlags::kUnloadedByDefault) != 0);
  CHECK((loaded->strings[0].flags() & ae::ObjFlags::kUnloaded) == 0);

  loaded->strings[0].Reset();
  {
    ae::Ptr<ui::UiString> en_owned = domain.Find(en_string_id);
    en_owned.Reset();
  }
  CHECK(!domain.Find(en_string_id));

  CHECK(!loaded->strings[1].is_loaded());
  CHECK(!domain.Find(ru_string_id));

  ui::UiLocalizationString::ptr::MakeFromThis(&*loaded).Save();

  CHECK(!loaded->strings[1].is_loaded());
  CHECK(!domain.Find(ru_string_id));

  ae::Domain reload{storage};
  auto reloaded = ui::UiLocalizationString::ptr::Declare(
      ae::CreateWith{reload}.with_id(root_id));
  reloaded.Load();
  CHECK(!reloaded->strings[0].is_loaded());
  CHECK(!reloaded->strings[1].is_loaded());

  reloaded->MaterializeLanguage(1);
  CHECK(!reloaded->strings[0].is_loaded());
  CHECK(reloaded->strings[1].is_loaded());
  CHECK(reloaded->strings[0].id() == en_string_id);
  CHECK(reloaded->strings[1].id() == ru_string_id);
  CHECK(reloaded->Resolve(1) == kRuUpdated);

  {
    auto en_view = reloaded->strings[0].Load();
    CHECK(en_view->utf8 == kEnText);
  }
}

void TestLocalizationLazyInitialPublication() {
  ae::RamDomainStorage storage;
  ae::ObjId const root_id{2};
  ae::ObjId en_string_id;
  ae::ObjId ru_string_id;
  char const* const kEnText = "Publication EN slot";
  char const* const kRuText = "Publication RU slot";

  {
    ae::Domain domain{storage};
    auto localized = ui::UiLocalizationString::ptr::Create(
        ae::CreateWith{domain}.with_id(root_id));
    InitializeRuntimeNode(*localized);

    auto en = ui::UiString::ptr::Create(ae::CreateWith{domain});
    en->utf8 = kEnText;
    auto ru = ui::UiString::ptr::Create(ae::CreateWith{domain});
    ru->utf8 = kRuText;
    en_string_id = en->obj_id;
    ru_string_id = ru->obj_id;
    en.Save();
    ru.Save();

    localized->strings = {
        ui::UiString::ptr::Declare(
            ae::CreateWith{domain}
                .with_id(en_string_id)
                .with_flags(ae::ObjFlags::kUnloadedByDefault)),
        ui::UiString::ptr::Declare(
            ae::CreateWith{domain}
                .with_id(ru_string_id)
                .with_flags(ae::ObjFlags::kUnloadedByDefault)),
    };
    ui::UiLocalizationString::ptr::MakeFromThis(&*localized).Save();
  }

  ae::Domain model_domain{storage};
  auto model_root = ui::UiLocalizationString::ptr::Declare(
      ae::CreateWith{model_domain}.with_id(root_id));
  model_root.Load();
  CHECK(!model_root->strings[0].is_loaded());
  CHECK(!model_root->strings[1].is_loaded());

  int materialize_notify = 0;
  model_root->BindMaterializedChangeNotifier(&materialize_notify, &PendingNotify);
  model_root->MaterializeLanguage(0);
  CHECK(materialize_notify == 1);
  CHECK(model_root->strings[0].is_loaded());
  CHECK(!model_root->strings[1].is_loaded());
  CHECK(model_root->Resolve(0) == kEnText);

  char const* const kRuUpdated = "Publication RU slot updated";
  {
    auto edit = SetUiLocalizationSlotUtf8Event::ptr::Create(
        ae::CreateWith{model_domain});
    edit->slot_index = 1;
    edit->utf8 = kRuUpdated;
    model_root->Commit(std::move(edit));
  }
  CHECK(model_root->strings[0].is_loaded());
  CHECK(!model_root->strings[1].is_loaded());
  CHECK(model_root->strings[0].id() == en_string_id);
  CHECK(model_root->strings[1].id() == ru_string_id);

  ByteSink sink;
  SerializeInitialPublication(*model_root, sink);

  CHECK(!model_root->strings[1].is_loaded());
  CHECK((model_root->strings[0].flags() & ae::ObjFlags::kUnloadedByDefault) != 0);
  CHECK((model_root->strings[1].flags() & ae::ObjFlags::kUnloadedByDefault) != 0);

  ae::RamDomainStorage ui_storage;
  ae::Domain ui_domain{ui_storage};
  std::vector<std::uint8_t> publication_bytes = sink.bytes;
  ByteSource in;
  in.data = publication_bytes.data();
  in.size = publication_bytes.size();
  auto ui_root_obj = LoadInitialPublication(in, ui_domain, ui_storage);
  CHECK(ui_root_obj);
  auto ui_root = ui::UiLocalizationString::ptr::MakeFromThis(
      static_cast<ui::UiLocalizationString*>(ui_root_obj.get()));

  CHECK(ui_root->strings[0].id() == en_string_id);
  CHECK(ui_root->strings[1].id() == ru_string_id);

  ae::ClassList const en_layers = ui_storage.Enumerate(en_string_id);
  ae::ClassList const ru_layers = ui_storage.Enumerate(ru_string_id);
  CHECK(!en_layers.empty());
  CHECK(ru_layers.empty());

  CHECK(ui_root->strings[0].is_loaded());
  CHECK(!ui_root->strings[1].is_loaded());
  CHECK(!ui_domain.Find(ru_string_id));

  CHECK(ui_root->Resolve(0) == kEnText);

  materialize_notify = 0;
  model_root->MaterializeLanguage(1);
  CHECK(materialize_notify == 1);
  model_root->MaterializeLanguage(1);
  CHECK(materialize_notify == 1);
  CHECK(!model_root->strings[0].is_loaded());
  CHECK(model_root->strings[1].is_loaded());
  CHECK(model_root->strings[0].id() == en_string_id);
  CHECK(model_root->strings[1].id() == ru_string_id);
  CHECK(model_root->Resolve(1) == kRuUpdated);

  ByteSink structural;
  SerializeStructuralNodePublication(*model_root, structural);
  CHECK(!model_root->strings[0].is_loaded());
  CHECK(model_root->strings[1].is_loaded());

  ByteSource structural_in;
  structural_in.data = structural.bytes.data();
  structural_in.size = structural.bytes.size();
  ApplyStructuralPublication(structural_in, ui_domain, ui_storage);

  CHECK(ui_root->strings[0].id() == en_string_id);
  CHECK(ui_root->strings[1].id() == ru_string_id);
  CHECK(!ui_root->strings[0].is_loaded());
  CHECK(ui_root->strings[1].is_loaded());
  CHECK(ui_root->Resolve(1) == kRuUpdated);
  ae::ClassList const ru_layers_after = ui_storage.Enumerate(ru_string_id);
  CHECK(!ru_layers_after.empty());
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
  TestLocalizationLazyLoadRoundtrip();
  TestLocalizationLazyInitialPublication();
  TestPushButtonClickViaPresenter();
  return 0;
}
