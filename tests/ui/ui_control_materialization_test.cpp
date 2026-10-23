#include <iostream>
#include <vector>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"

#include "apptraverse/distill.h"
#include "apptraverse/model_object_proxy.h"
#include "apptraverse/object_macros.h"
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

class TestLazyWindowsRoot : public ae::Obj {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::TestLazyWindowsRoot",
                           TestLazyWindowsRoot, ae::Obj, 0)

 protected:
  TestLazyWindowsRoot() = default;

 public:
  explicit TestLazyWindowsRoot(ae::ObjProp prop) : ae::Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(window_a), AE_MMBR(window_b), AE_MMBR(presenter_a),
                    AE_MMBR(presenter_b))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, window_a, window_b, presenter_a, presenter_b);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, window_a, window_b, presenter_a, presenter_b);
  }

  ui::UiWindow::ptr window_a;
  ui::UiWindow::ptr window_b;
  ui::UiWindowPresenter::ptr presenter_a;
  ui::UiWindowPresenter::ptr presenter_b;
};

APPTRAVERSE_REGISTER(TestLazyWindowsRoot);

void EnsureTestRegistration() {
  (void)&g_apptraverse_registrar_TestClickButton;
  (void)&g_apptraverse_registrar_TestLazyWindowsRoot;
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

void TestWindowLazyStateRoundtrip() {
  ae::RamDomainStorage storage;
  ae::ObjId window_a_id;
  ae::ObjId window_b_id;
  ae::ObjId window_a_base_id;
  ae::ObjId window_a_event0_id;

  {
    ae::Domain domain{storage};
    auto window_a = UiWindow::ptr::Create(
        ae::CreateWith{domain}.with_flags(ae::ObjFlags::kUnloadedByDefault));
    InitializeRuntimeNode(*window_a);
    auto window_b = UiWindow::ptr::Create(
        ae::CreateWith{domain}.with_flags(ae::ObjFlags::kUnloadedByDefault));
    InitializeRuntimeNode(*window_b);

    auto set_a = SetUiWindowClientSizeEvent::ptr::Create(
        ae::CreateWith{domain});
    set_a->width = 640;
    set_a->height = 480;
    window_a->Commit(std::move(set_a));

    auto set_b = SetUiWindowClientSizeEvent::ptr::Create(
        ae::CreateWith{domain});
    set_b->width = 900;
    set_b->height = 700;
    window_b->Commit(std::move(set_b));

    window_a_id = window_a->obj_id;
    window_b_id = window_b->obj_id;
    window_a_base_id = window_a->base->obj_id;
    CHECK(window_a->journal.size() == 1);
    window_a_event0_id = window_a->journal[0].event->obj_id;

    UiWindow::ptr::MakeFromThis(&*window_a).Save();
    UiWindow::ptr::MakeFromThis(&*window_b).Save();
  }

  {
    ae::Domain domain{storage};
    UiWindow::ptr window_a = UiWindow::ptr::Declare(
        ae::CreateWith{domain}
            .with_id(window_a_id)
            .with_flags(ae::ObjFlags::kUnloadedByDefault));
    UiWindow::ptr window_b = UiWindow::ptr::Declare(
        ae::CreateWith{domain}
            .with_id(window_b_id)
            .with_flags(ae::ObjFlags::kUnloadedByDefault));

    window_a.Load();
    CHECK(window_a->client_width() == 640);
    CHECK(window_a->client_height() == 480);
    CHECK(window_a->obj_id == window_a_id);
    CHECK(window_a->base->obj_id == window_a_base_id);
    CHECK(window_a->journal.size() == 1);
    CHECK(window_a->journal[0].event->obj_id == window_a_event0_id);

    CHECK(!window_b.is_loaded());
    CHECK(!domain.Find(window_b_id));

    ae::RamDomainStorage ui_storage;
    ae::Domain ui_domain{ui_storage};
    ByteSink initial_sink;
    SerializeInitialPublication(*window_a, initial_sink);
    ByteSource initial_in;
    initial_in.data = initial_sink.bytes.data();
    initial_in.size = initial_sink.bytes.size();
    auto ui_root_obj = LoadInitialPublication(initial_in, ui_domain, ui_storage);
    CHECK(ui_root_obj);
    auto ui_a = UiWindow::ptr::MakeFromThis(
        static_cast<UiWindow*>(ui_root_obj.get()));
    UiWindow* const ui_a_object = &*ui_a;
    CHECK(ui_a->obj_id == window_a_id);
    CHECK(ui_a->client_width() == 640);
    CHECK(ui_a->client_height() == 480);
    CHECK(!ui_a->base.is_valid());
    CHECK(ui_a->journal.empty());
    CHECK(!ui_storage.Enumerate(window_a_id).empty());
    CHECK(ui_storage.Enumerate(window_a_base_id).empty());
    CHECK(ui_storage.Enumerate(window_a_event0_id).empty());
    CHECK(ui_storage.Enumerate(window_b_id).empty());
    CHECK(window_a->journal.size() == 1);
    CHECK(!window_b.is_loaded());
    CHECK(!domain.Find(window_b_id));

    auto const journal_before = window_a->journal.size();
    auto resize = SetUiWindowClientSizeEvent::ptr::Create(
        ae::CreateWith{domain});
    resize->width = 740;
    resize->height = 530;
    window_a->Commit(std::move(resize));
    CHECK(window_a->journal.size() == journal_before + 1);

    ae::ObjId const window_a_event1_id = window_a->journal[1].event->obj_id;
    auto const model_generation = window_a->Generation();

    ByteSink structural_sink;
    SerializeStructuralNodePublication(*window_a, structural_sink);
    ByteSource structural_in;
    structural_in.data = structural_sink.bytes.data();
    structural_in.size = structural_sink.bytes.size();
    ApplyStructuralPublication(structural_in, ui_domain, ui_storage);

    CHECK(&*ui_a == ui_a_object);
    CHECK(ui_a->obj_id == window_a_id);
    CHECK(ui_a->client_width() == 740);
    CHECK(ui_a->client_height() == 530);
    CHECK(ui_a->Generation() == model_generation);
    CHECK(!ui_a->base.is_valid());
    CHECK(ui_a->journal.empty());
    CHECK(ui_storage.Enumerate(window_a_base_id).empty());
    CHECK(ui_storage.Enumerate(window_a_event0_id).empty());
    CHECK(ui_storage.Enumerate(window_a_event1_id).empty());
    CHECK(ui_storage.Enumerate(window_b_id).empty());
    CHECK(window_a->journal.size() == 2);
    CHECK(window_a->journal[0].event->obj_id == window_a_event0_id);
    CHECK(window_a->journal[1].event->obj_id == window_a_event1_id);
    CHECK(!window_b.is_loaded());
    CHECK(!domain.Find(window_b_id));

    window_a.Save();
    window_a.Reset();
    CHECK(window_a.id() == window_a_id);
    CHECK((window_a.flags() & ae::ObjFlags::kUnloadedByDefault) != 0);
    CHECK((window_a.flags() & ae::ObjFlags::kUnloaded) != 0);
    CHECK(!domain.Find(window_a_id));
    CHECK(!window_b.is_loaded());
    CHECK(!domain.Find(window_b_id));

    window_a.Load();
    CHECK(window_a->client_width() == 740);
    CHECK(window_a->client_height() == 530);
    CHECK(window_a->base->obj_id == window_a_base_id);
    CHECK(window_a->journal.size() == 2);
    CHECK(window_a->journal[0].event->obj_id == window_a_event0_id);
    CHECK(!window_b.is_loaded());
    CHECK(!domain.Find(window_b_id));
  }

  {
    ae::Domain domain{storage};
    UiWindow::ptr window_a = UiWindow::ptr::Declare(
        ae::CreateWith{domain}.with_id(window_a_id));
    window_a.Load();
    CHECK(window_a->client_width() == 740);
    CHECK(window_a->client_height() == 530);
    CHECK(window_a->journal.size() == 2);
    CHECK(window_a->base->obj_id == window_a_base_id);

    UiWindow::ptr window_b = UiWindow::ptr::Declare(
        ae::CreateWith{domain}.with_id(window_b_id));
    window_b.Load();
    CHECK(window_b->client_width() == 900);
    CHECK(window_b->client_height() == 700);
  }
}

void TestWindowLazyGraphInitialPublication() {
  ae::RamDomainStorage storage;
  ae::ObjId const root_id{0x83000001u};
  ae::ObjId window_a_id;
  ae::ObjId window_b_id;
  ae::ObjId presenter_a_id;
  ae::ObjId presenter_b_id;
  ae::ObjId window_a_base_id;
  ae::ObjId window_a_event0_id;
  ae::ObjId window_b_base_id;
  ae::ObjId window_b_event0_id;

  {
    ae::Domain domain{storage};
    auto window_a = UiWindow::ptr::Create(
        ae::CreateWith{domain}.with_flags(ae::ObjFlags::kUnloadedByDefault));
    InitializeRuntimeNode(*window_a);
    auto window_b = UiWindow::ptr::Create(
        ae::CreateWith{domain}.with_flags(ae::ObjFlags::kUnloadedByDefault));
    InitializeRuntimeNode(*window_b);

    auto set_a = SetUiWindowClientSizeEvent::ptr::Create(
        ae::CreateWith{domain});
    set_a->width = 640;
    set_a->height = 480;
    window_a->Commit(std::move(set_a));

    auto set_b = SetUiWindowClientSizeEvent::ptr::Create(
        ae::CreateWith{domain});
    set_b->width = 900;
    set_b->height = 700;
    window_b->Commit(std::move(set_b));

    window_a_id = window_a->obj_id;
    window_b_id = window_b->obj_id;
    window_a_event0_id = window_a->journal[0].event->obj_id;

    auto presenter_a = ui::UiWindowPresenter::ptr::Create(
        ae::CreateWith{domain}.with_flags(ae::ObjFlags::kUnloadedByDefault));
    auto presenter_b = ui::UiWindowPresenter::ptr::Create(
        ae::CreateWith{domain}.with_flags(ae::ObjFlags::kUnloadedByDefault));
    presenter_a->window = window_a;
    presenter_b->window = window_b;
    presenter_a->window.SetFlags(presenter_a->window.flags() &
                                 ~ae::ObjFlags::kUnloadedByDefault);
    presenter_b->window.SetFlags(presenter_b->window.flags() &
                                 ~ae::ObjFlags::kUnloadedByDefault);
    presenter_a_id = presenter_a->obj_id;
    presenter_b_id = presenter_b->obj_id;

    auto root = TestLazyWindowsRoot::ptr::Create(
        ae::CreateWith{domain}.with_id(root_id));

    UiWindow::ptr::MakeFromThis(&*window_a).Save();
    UiWindow::ptr::MakeFromThis(&*window_b).Save();
    ui::UiWindowPresenter::ptr::MakeFromThis(&*presenter_a).Save();
    ui::UiWindowPresenter::ptr::MakeFromThis(&*presenter_b).Save();

    root->window_a = UiWindow::ptr::Declare(
        ae::CreateWith{domain}
            .with_id(window_a_id)
            .with_flags(ae::ObjFlags::kUnloadedByDefault));
    root->window_b = UiWindow::ptr::Declare(
        ae::CreateWith{domain}
            .with_id(window_b_id)
            .with_flags(ae::ObjFlags::kUnloadedByDefault));
    root->presenter_a = ui::UiWindowPresenter::ptr::Declare(
        ae::CreateWith{domain}
            .with_id(presenter_a_id)
            .with_flags(ae::ObjFlags::kUnloadedByDefault));
    root->presenter_b = ui::UiWindowPresenter::ptr::Declare(
        ae::CreateWith{domain}
            .with_id(presenter_b_id)
            .with_flags(ae::ObjFlags::kUnloadedByDefault));
    TestLazyWindowsRoot::ptr::MakeFromThis(&*root).Save();
  }

  {
    ae::Domain domain{storage};
    TestLazyWindowsRoot::ptr root = TestLazyWindowsRoot::ptr::Declare(
        ae::CreateWith{domain}.with_id(root_id));
    root.Load();

    CHECK(root->window_a.is_valid());
    CHECK(root->window_b.is_valid());
    CHECK(root->window_a.id() == window_a_id);
    CHECK(root->window_b.id() == window_b_id);
    CHECK((root->window_a.flags() & ae::ObjFlags::kUnloadedByDefault) != 0);
    CHECK((root->window_b.flags() & ae::ObjFlags::kUnloadedByDefault) != 0);
    CHECK((root->presenter_a.flags() & ae::ObjFlags::kUnloadedByDefault) != 0);
    CHECK((root->presenter_b.flags() & ae::ObjFlags::kUnloadedByDefault) != 0);
    CHECK(!root->window_a.is_loaded());
    CHECK(!root->window_b.is_loaded());
    CHECK(!root->presenter_a.is_loaded());
    CHECK(!root->presenter_b.is_loaded());
    CHECK(!domain.Find(window_a_id));
    CHECK(!domain.Find(window_b_id));
    CHECK(!domain.Find(presenter_a_id));
    CHECK(!domain.Find(presenter_b_id));

    root->window_a.Load();
    root->presenter_a.Load();
    CHECK(root->window_a->client_width() == 640);
    CHECK(root->window_a->client_height() == 480);
    CHECK(root->window_a->journal.size() == 1);
    CHECK(root->window_a->journal[0].event->obj_id == window_a_event0_id);
    window_a_base_id = root->window_a->base->obj_id;

    CHECK(!root->presenter_a->presentation_loaded);

    CHECK(!root->presenter_b.is_loaded());
    CHECK(!root->window_b.is_loaded());
    CHECK(!domain.Find(presenter_b_id));
    CHECK(!domain.Find(window_b_id));

    ae::RamDomainStorage ui_storage;
    ae::Domain ui_domain{ui_storage};
    ByteSink initial_sink;
    SerializeInitialPublication(*root, initial_sink);

    CHECK(!root->presenter_b.is_loaded());
    CHECK(!root->window_b.is_loaded());
    CHECK(!domain.Find(presenter_b_id));
    CHECK(!domain.Find(window_b_id));

    ByteSource initial_in;
    initial_in.data = initial_sink.bytes.data();
    initial_in.size = initial_sink.bytes.size();
    auto ui_root_obj = LoadInitialPublication(initial_in, ui_domain, ui_storage);
    CHECK(ui_root_obj);
    auto ui_root = TestLazyWindowsRoot::ptr::MakeFromThis(
        static_cast<TestLazyWindowsRoot*>(ui_root_obj.get()));
    TestLazyWindowsRoot* const ui_root_object = &*ui_root;

    CHECK(ui_root->window_a.id() == window_a_id);
    CHECK(ui_root->window_b.id() == window_b_id);
    CHECK(ui_root->presenter_a.id() == presenter_a_id);
    CHECK(ui_root->presenter_b.id() == presenter_b_id);
    CHECK(ui_root->window_a.is_loaded());
    CHECK(ui_root->window_a->client_width() == 640);
    CHECK(ui_root->window_a->client_height() == 480);
    CHECK(!ui_root->window_b.is_loaded());
    CHECK(!ui_domain.Find(window_b_id));
    CHECK(!ui_root->presenter_b.is_loaded());
    CHECK(!ui_domain.Find(presenter_b_id));
    CHECK(ui_storage.Enumerate(presenter_b_id).empty());

    InitializePresenters(*ui_root);
    CHECK(ui_root->presenter_a.is_loaded());
    CHECK(ui_root->presenter_a->presentation_loaded);
    CHECK(ui_root->presenter_a->presentation_load_order != 0);
    CHECK(ui_root->presenter_a->window.is_loaded());
    CHECK(ui_root->presenter_a->window.id() == window_a_id);
    CHECK(&*ui_root->presenter_a->window == &*ui_root->window_a);
    CHECK(!ui_root->presenter_b.is_loaded());
    CHECK(!ui_domain.Find(presenter_b_id));
    CHECK(!root->presenter_a->presentation_loaded);

    CHECK(!ui_storage.Enumerate(root_id).empty());
    CHECK(!ui_storage.Enumerate(window_a_id).empty());
    CHECK(ui_storage.Enumerate(window_b_id).empty());

    CHECK(!ui_root->window_a->base.is_valid());
    CHECK(ui_root->window_a->journal.empty());
    CHECK(ui_storage.Enumerate(window_a_base_id).empty());
    CHECK(ui_storage.Enumerate(window_a_event0_id).empty());

    CHECK(root->window_a->journal.size() == 1);
    CHECK(root->window_a->journal[0].event->obj_id == window_a_event0_id);
    root->window_b.Load();
    root->presenter_b.Load();
    CHECK(!root->presenter_b->presentation_loaded);
    CHECK(root->window_b->client_width() == 900);
    CHECK(root->window_b->client_height() == 700);
    CHECK(root->window_b->journal.size() == 1);
    window_b_event0_id = root->window_b->journal[0].event->obj_id;
    window_b_base_id = root->window_b->base->obj_id;

    root->window_a.Save();
    root->presenter_a.Save();
    root->window_a.Reset();
    root->presenter_a.Reset();
    CHECK(root->window_a.is_valid());
    CHECK(root->window_a.id() == window_a_id);
    CHECK(root->presenter_a.is_valid());
    CHECK(root->presenter_a.id() == presenter_a_id);
    CHECK((root->window_a.flags() & ae::ObjFlags::kUnloadedByDefault) != 0);
    CHECK((root->presenter_a.flags() & ae::ObjFlags::kUnloadedByDefault) != 0);
    CHECK(!root->window_a.is_loaded());
    CHECK(!root->presenter_a.is_loaded());
    CHECK(!domain.Find(window_a_id));
    CHECK(!domain.Find(presenter_a_id));
    CHECK(root->window_b.is_loaded());
    CHECK(root->presenter_b.is_loaded());

    ByteSink graph_sink;
    SerializeObjectGraphToBuffer(*root, graph_sink);
    {
      StructuralPresentationKeepalive keepalive =
          CaptureStructuralPresentationKeepalive(*ui_root);
      ByteSource graph_in;
      graph_in.data = graph_sink.bytes.data();
      graph_in.size = graph_sink.bytes.size();
      DeserializeObjectGraphFromBuffer(*ui_root, graph_in, ui_domain,
                                         ui_storage);
      UpdatePresentersAfterStructuralPublication(*ui_root,
                                                 keepalive.active_presenters);

      Presenter* keepalive_presenter_a = nullptr;
      for (Presenter::ptr const& held : keepalive.active_presenters) {
        if (held.is_valid() && held.is_loaded() &&
            held.id() == presenter_a_id) {
          keepalive_presenter_a = &*held;
          break;
        }
      }
      CHECK(keepalive_presenter_a != nullptr);
      CHECK(!keepalive_presenter_a->presentation_loaded);

      CHECK(&*ui_root == ui_root_object);
      CHECK(ui_root->obj_id == root_id);
      CHECK(ui_root->window_a.id() == window_a_id);
      CHECK(ui_root->window_b.id() == window_b_id);
      CHECK(ui_root->presenter_a.id() == presenter_a_id);
      CHECK(ui_root->presenter_b.id() == presenter_b_id);
      CHECK(!ui_root->window_a.is_loaded());
      CHECK(ui_root->window_b.is_loaded());
      CHECK(ui_root->presenter_b.is_loaded());
      CHECK(ui_root->presenter_b->presentation_loaded);
      CHECK(ui_root->presenter_b->presentation_load_order != 0);
      CHECK(ui_root->presenter_b->window.is_loaded());
      CHECK(ui_root->presenter_b->window.id() == window_b_id);
      CHECK(&*ui_root->presenter_b->window == &*ui_root->window_b);
      CHECK(ui_root->window_b->client_width() == 900);
      CHECK(ui_root->window_b->client_height() == 700);
      CHECK(!ui_root->window_b->base.is_valid());
      CHECK(ui_root->window_b->journal.empty());
      CHECK(ui_storage.Enumerate(window_b_base_id).empty());
      CHECK(ui_storage.Enumerate(window_b_event0_id).empty());
      CHECK(!ui_storage.Enumerate(window_b_id).empty());

      CHECK(root->window_b->journal.size() == 1);
      CHECK(root->window_b->journal[0].event->obj_id == window_b_event0_id);
      CHECK(!root->window_a.is_loaded());
      CHECK(!domain.Find(window_a_id));
      CHECK(!domain.Find(presenter_a_id));
      CHECK(!storage.Enumerate(window_a_id).empty());

      keepalive.active_presenters.clear();
      keepalive.live_objects.clear();
      CHECK(!ui_domain.Find(window_a_id));
      CHECK(!ui_domain.Find(presenter_a_id));
    }
    CHECK(!ui_domain.Find(window_a_id));
    CHECK(!ui_domain.Find(presenter_a_id));
    CHECK(!ui_root->window_a.is_loaded());
    CHECK(!ui_root->presenter_a.is_loaded());
    CHECK(ui_root->window_b.is_loaded());
    CHECK(ui_root->window_b->client_width() == 900);

    UnloadPresenters(*ui_root);
    CHECK(ui_root->presenter_b.is_loaded());
    CHECK(!ui_root->presenter_b->presentation_loaded);
    CHECK(ui_root->presenter_b->presentation_load_order == 0);
  }
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
  TestWindowLazyStateRoundtrip();
  TestWindowLazyGraphInitialPublication();
  TestPushButtonClickViaPresenter();
  return 0;
}
