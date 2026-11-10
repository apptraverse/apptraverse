#include <chrono>
#include <deque>
#include <filesystem>
#include <iostream>
#include <type_traits>
#include <vector>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/distill.h"
#include "apptraverse/journal_retention_policy.h"
#include "apptraverse/model_object_proxy.h"
#include "apptraverse/object_serialization.h"
#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/runtime_node.h"
#include "apptraverse/node.h"
#include "apptraverse/ui/ui_list_container.h"
#include "apptraverse/ui/ui_localization_string.h"
#include "apptraverse/ui/ui_push_button_presenter.h"
#include "apptraverse/ui/ui_presenters.h"
#include "apptraverse/ui/ui_push_button.h"
#include "apptraverse/ui/ui_string.h"
#include "apptraverse/ui/ui_window.h"

using namespace apptraverse;
using namespace apptraverse::ui;

namespace apptraverse::test {

class DerivedUiLocalizationString
    : public NodeFor<DerivedUiLocalizationString, UiLocalizationString> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::DerivedUiLocalizationString",
                           DerivedUiLocalizationString, UiLocalizationString, 0)

 protected:
  DerivedUiLocalizationString() = default;

 public:
  explicit DerivedUiLocalizationString(ae::ObjProp prop) : NodeFor{prop} {}
};

APPTRAVERSE_REGISTER(DerivedUiLocalizationString);

}  // namespace apptraverse::test

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
  (void)&apptraverse::test::g_apptraverse_registrar_DerivedUiLocalizationString;
}

void PendingNotify(void* ctx, Node&) {
  auto* count = static_cast<int*>(ctx);
  ++(*count);
}

void TestExplicitEmptyLocalization() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto localized = UiLocalizationString::ptr::Create(ae::CreateWith{domain});
  auto text = UiString::ptr::Create(ae::CreateWith{domain});
  text->utf8 = "";
  localized->strings.push_back(text);
  InitializeRuntimeNode(*localized);
  localized->MaterializeLanguage(0);
  CHECK(localized->strings[0].is_loaded());
  CHECK(localized->Resolve(0).empty());
  CHECK(ResolveLocalization(localized.operator->(), 0).empty());
  text->utf8 = "Window {0}";
  CHECK(ResolveLocalization(localized.operator->(), 0) == "Window {0}");
}

void TestImmediateCompactionKeepsInactiveLanguageLazy() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto localized = ui::UiLocalizationString::ptr::Create(ae::CreateWith{domain});
  InitializeRuntimeNode(*localized);
  for (auto text : {"English", "Russian"}) {
    auto leaf = ui::UiString::ptr::Create(ae::CreateWith{domain});
    leaf->utf8 = text;
    leaf.Save();
    localized->strings.push_back(leaf);
  }
  localized->CaptureBaseState();
  localized->MaterializeLanguage(0);
  localized->CaptureBaseState();
  CHECK(localized->strings[0].is_loaded());
  CHECK(!localized->strings[1].is_loaded());
  localized->SetImmediateEventCompaction(true);
  auto before = storage.state;
  auto edit = SetUiLocalizationSlotUtf8Event::ptr::Create(ae::CreateWith{domain});
  edit->slot_index = 0;
  edit->utf8 = "Changed English";
  localized->Commit(edit);
  CHECK(localized->journal.empty());
  CHECK(localized->strings[0].is_loaded());
  CHECK(!localized->strings[1].is_loaded());
  CHECK(localized->Resolve(0) == "Changed English");
  CHECK(storage.state == before);
  localized->MaterializeLanguage(1);
  CHECK(localized->Resolve(1) == "Russian");
  CHECK(!localized->strings[0].is_loaded());
  localized->MaterializeLanguage(0);
  CHECK(localized->Resolve(0) == "Changed English");
}

void TestPushButtonMaterializationNotifiesOnChange() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto doc = UiWindow::ptr::Create(ae::CreateWith{domain});
  doc->BootstrapClientSize(800, 600);
  InitializeRuntimeNode(*doc);
  auto enable = SetUiPushButtonEnabledEvent::ptr::Create(
      ae::CreateWith{domain});
  auto button = TestClickButton::ptr::Create(ae::CreateWith{domain});
  button->BootstrapSetEnabled(true);
  InitializeRuntimeNode(*button, *doc);
  int button_notify = 0;
  button->BindMaterializedChangeNotifier(&button_notify, &PendingNotify);
  enable->enabled = false;
  button->Commit(enable);
  CHECK(button_notify == 1);
  CHECK(!button->enabled());

  auto const generation = button->Generation();
  auto duplicate = SetUiPushButtonEnabledEvent::ptr::Create(
      ae::CreateWith{domain});
  duplicate->enabled = false;
  button->Commit(duplicate);
  CHECK(button->Generation() == generation);
  CHECK(button_notify == 1);
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

std::string Utf8FromStorageOnly(ae::RamDomainStorage& storage, ae::ObjId id) {
  ae::Domain domain{storage};
  auto string = ui::UiString::ptr::Declare(ae::CreateWith{domain}.with_id(id));
  string.Load();
  std::string const utf8 = string->utf8;
  string.Reset();
  return utf8;
}

std::string Utf8FromDirectoryStorage(DirectoryDomainStorage& storage,
                                    ae::ObjId id) {
  ae::Domain domain{storage};
  auto string = ui::UiString::ptr::Declare(ae::CreateWith{domain}.with_id(id));
  string.Load();
  std::string const utf8 = string->utf8;
  string.Reset();
  return utf8;
}

std::filesystem::path LocalizationTestDir(char const* name) {
  auto const dir =
      std::filesystem::temp_directory_path() /
      ("apptraverse_ui_localization_" + std::string{name} + "_" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir, ec);
  CHECK(!ec);
  return dir;
}

struct LazyLocalizationBootstrap {
  ae::RamDomainStorage storage;
  ae::ObjId root_id;
  ae::ObjId en_string_id;
  ae::ObjId ru_string_id;
  char const* en_text;
  char const* ru_text;

  LazyLocalizationBootstrap(ae::ObjId root, char const* en, char const* ru)
      : root_id{root}, en_text{en}, ru_text{ru} {
    ae::Domain domain{storage};
    auto localized = ui::UiLocalizationString::ptr::Create(
        ae::CreateWith{domain}.with_id(root_id));
    InitializeRuntimeNode(*localized);

    auto en_string = ui::UiString::ptr::Create(ae::CreateWith{domain});
    en_string->utf8 = en_text;
    auto ru_string = ui::UiString::ptr::Create(ae::CreateWith{domain});
    ru_string->utf8 = ru_text;
    en_string_id = en_string->obj_id;
    ru_string_id = ru_string->obj_id;
    en_string.Save();
    ru_string.Save();

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

  ui::UiLocalizationString::ptr LoadRoot(ae::Domain& domain) {
    auto localized = ui::UiLocalizationString::ptr::Declare(
        ae::CreateWith{domain}.with_id(root_id));
    localized.Load();
    return localized;
  }
};

std::size_t CountLocalizationUtf8Events(ui::UiLocalizationString const& localized) {
  std::size_t count = 0;
  for (auto const& record : localized.journal) {
    if (record.event &&
        record.event->GetClassId() == SetUiLocalizationSlotUtf8Event::kClassId) {
      ++count;
    }
  }
  return count;
}

void CommitSlotUtf8(ui::UiLocalizationString& localized, ae::Domain& domain,
                    int slot, char const* utf8) {
  auto event = SetUiLocalizationSlotUtf8Event::ptr::Create(
      ae::CreateWith{domain});
  event->slot_index = slot;
  event->utf8 = utf8;
  localized.Commit(std::move(event));
}

void TestLocalizationInactiveSlotAbaWithoutPersist() {
  char const* const kStorageA = "Slot-A";
  LazyLocalizationBootstrap boot{20, "Slot-EN-active", kStorageA};
  ae::Domain domain{boot.storage};
  auto localized = boot.LoadRoot(domain);
  localized->MaterializeLanguage(0);
  CHECK(!localized->strings[1].is_loaded());
  CHECK(Utf8FromStorageOnly(boot.storage, boot.ru_string_id) == kStorageA);

  CommitSlotUtf8(*localized, domain, 1, "Slot-B");
  CommitSlotUtf8(*localized, domain, 1, kStorageA);
  CHECK(!localized->strings[1].is_loaded());
  CHECK(Utf8FromStorageOnly(boot.storage, boot.ru_string_id) == kStorageA);

  localized->MaterializeLanguage(1);
  CHECK(localized->Resolve(1) == kStorageA);
  localized->MaterializeLanguage(0);
  CHECK(!localized->strings[1].is_loaded());
}

void TestLocalizationSequentialSlotEditsLastWins() {
  LazyLocalizationBootstrap boot{21, "EN", "RU0"};
  ae::Domain domain{boot.storage};
  auto localized = boot.LoadRoot(domain);
  localized->MaterializeLanguage(0);

  CommitSlotUtf8(*localized, domain, 1, "RU1");
  CommitSlotUtf8(*localized, domain, 1, "RU2");
  CommitSlotUtf8(*localized, domain, 1, "RU-final");
  localized->MaterializeLanguage(1);
  CHECK(localized->Resolve(1) == "RU-final");
}

void TestLocalizationLanguageSwitchWithoutImplicitPersist() {
  ae::RamDomainStorage storage;
  ae::ObjId const root_id{11};
  ae::ObjId en_string_id;
  ae::ObjId ru_string_id;
  char const* const kEnText = "Persist EN";
  char const* const kRuText = "Persist RU";
  char const* const kEnEdited = "Session EN";
  char const* const kRuEdited = "Session RU";

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

  ae::ObjId en_id;
  ae::ObjId ru_id;
  {
    ae::Domain domain{storage};
    auto localized = ui::UiLocalizationString::ptr::Declare(
        ae::CreateWith{domain}.with_id(root_id));
    localized.Load();
    localized->MaterializeLanguage(0);
    en_id = localized->strings[0].id();
    ru_id = localized->strings[1].id();
    std::size_t const ru_layer_count_bootstrap =
        storage.Enumerate(ru_string_id).size();

    auto edit_en = SetUiLocalizationSlotUtf8Event::ptr::Create(
        ae::CreateWith{domain});
    edit_en->slot_index = 0;
    edit_en->utf8 = kEnEdited;
    localized->Commit(std::move(edit_en));
    CHECK(localized->Resolve(0) == kEnEdited);
    CHECK(Utf8FromStorageOnly(storage, en_string_id) == kEnText);
    CHECK(storage.Enumerate(ru_string_id).size() == ru_layer_count_bootstrap);

    localized->MaterializeLanguage(1);
    auto edit_ru = SetUiLocalizationSlotUtf8Event::ptr::Create(
        ae::CreateWith{domain});
    edit_ru->slot_index = 1;
    edit_ru->utf8 = kRuEdited;
    localized->Commit(std::move(edit_ru));
    CHECK(localized->Resolve(1) == kRuEdited);
    CHECK(!localized->strings[0].is_loaded());
    CHECK(storage.Enumerate(ru_string_id).size() == ru_layer_count_bootstrap);
    CHECK(Utf8FromStorageOnly(storage, ru_string_id) == kRuText);

    localized->MaterializeLanguage(0);
    CHECK(localized->Resolve(0) == kEnEdited);
    localized->MaterializeLanguage(1);
    CHECK(localized->Resolve(1) == kRuEdited);
    localized->MaterializeLanguage(0);
    CHECK(localized->strings[0].id() == en_id);
    CHECK(localized->strings[1].id() == ru_id);
    CHECK(Utf8FromStorageOnly(storage, en_string_id) == kEnText);
    CHECK(Utf8FromStorageOnly(storage, ru_string_id) == kRuText);

    ui::UiLocalizationString::ptr::MakeFromThis(&*localized).Save();
    localized.Reset();
  }

  ae::Domain cold_domain{storage};
  auto cold = ui::UiLocalizationString::ptr::Declare(
      ae::CreateWith{cold_domain}.with_id(root_id));
  cold.Load();
  cold->MaterializeLanguage(0);
  CHECK(cold->Resolve(0) == kEnEdited);
  cold->MaterializeLanguage(1);
  CHECK(cold->Resolve(1) == kRuEdited);
  CHECK(cold->strings[0].id() == en_id);
  CHECK(cold->strings[1].id() == ru_id);
}

void TestLocalizationDirectoryLifecycleWithoutSessionWrites() {
  auto const dir = LocalizationTestDir("directory_lifecycle");
  ae::ObjId const root_id{22};
  ae::ObjId en_string_id;
  ae::ObjId ru_string_id;

  {
    DirectoryDomainStorage storage{dir};
    ae::Domain domain{storage};
    auto localized = ui::UiLocalizationString::ptr::Create(
        ae::CreateWith{domain}.with_id(root_id));
    InitializeRuntimeNode(*localized);

    auto en = ui::UiString::ptr::Create(ae::CreateWith{domain});
    en->utf8 = "Disk EN";
    auto ru = ui::UiString::ptr::Create(ae::CreateWith{domain});
    ru->utf8 = "Disk RU";
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
    storage.BeginSaveSession();
    ui::UiLocalizationString::ptr::MakeFromThis(&*localized).Save();
    CHECK(!storage.SaveSessionHadFailure());
  }

  {
    DirectoryDomainStorage disk{dir};
    InstrumentedDomainStorage session_storage{disk, dir, &disk};
    InstrumentedDomainStorageStats const storage_quiet =
        session_storage.SnapshotStats();

    ae::Domain domain{session_storage};
    auto localized = ui::UiLocalizationString::ptr::Declare(
        ae::CreateWith{domain}.with_id(root_id));
    localized.Load();
    localized->MaterializeLanguage(0);

    CommitSlotUtf8(*localized, domain, 0, "Disk EN edited");
    localized->MaterializeLanguage(1);
    CommitSlotUtf8(*localized, domain, 1, "Disk RU edited");
    localized->MaterializeLanguage(0);
    CommitSlotUtf8(*localized, domain, 0, "Disk EN edited");
    localized->MaterializeLanguage(1);

    localized->SetJournalRetentionPolicy(JournalRetentionPolicy{.max_events = 0});
    localized->CompactJournal(SystemUtcMicros());
    CHECK(localized->journal.empty());
    localized->MaterializeLanguage(0);
    CHECK(localized->Resolve(0) == "Disk EN edited");
    localized->MaterializeLanguage(1);
    CHECK(localized->Resolve(1) == "Disk RU edited");

    InstrumentedDomainStorageStats const storage_after_edits =
        session_storage.SnapshotStats();
    CHECK(storage_after_edits.store_calls == storage_quiet.store_calls);
    CHECK(storage_after_edits.store_overwrite_layers ==
          storage_quiet.store_overwrite_layers);
    CHECK(storage_after_edits.store_disk_bytes_written ==
          storage_quiet.store_disk_bytes_written);

    localized->MaterializeLanguage(0);
    CHECK(localized->Resolve(0) == "Disk EN edited");
    localized->MaterializeLanguage(1);
    CHECK(localized->Resolve(1) == "Disk RU edited");

    disk.BeginSaveSession();
    ui::UiLocalizationString::ptr::MakeFromThis(&*localized).Save();
    CHECK(!disk.SaveSessionHadFailure());

    localized.Reset();
  }

  DirectoryDomainStorage cold_storage{dir};
  ae::Domain cold_domain{cold_storage};
  auto cold = ui::UiLocalizationString::ptr::Declare(
      ae::CreateWith{cold_domain}.with_id(root_id));
  cold.Load();
  cold->MaterializeLanguage(0);
  CHECK(cold->Resolve(0) == "Disk EN edited");
  cold->MaterializeLanguage(1);
  CHECK(cold->Resolve(1) == "Disk RU edited");
  CHECK(cold->strings[0].id() == en_string_id);
  CHECK(cold->strings[1].id() == ru_string_id);
}

void TestLocalizationJournalCompactionMatchesLoad() {
  LazyLocalizationBootstrap boot{23, "Compact EN", "Compact RU"};
  ae::ObjId en_id;
  ae::ObjId ru_id;
  {
    ae::Domain domain{boot.storage};
    auto localized = boot.LoadRoot(domain);
    localized->CaptureBaseState();
    localized->MaterializeLanguage(0);
    CommitSlotUtf8(*localized, domain, 0, "Compact EN v1");
    CommitSlotUtf8(*localized, domain, 1, "Compact RU v1");
    CHECK(!localized->strings[1].is_loaded());
    CHECK(CountLocalizationUtf8Events(*localized) >= 2);
    CHECK(!localized->journal.empty());

    en_id = localized->strings[0].id();
    ru_id = localized->strings[1].id();

    localized->MaterializeLanguage(0);
    CHECK(!localized->strings[1].is_loaded());
    localized->strings[1].Reset();
    CHECK(!localized->strings[1].is_loaded());

    localized->SetJournalRetentionPolicy(JournalRetentionPolicy{.max_events = 0});
    localized->CompactJournal(SystemUtcMicros());
    CHECK(localized->journal.empty());

    localized->MaterializeLanguage(0);
    CHECK(localized->Resolve(0) == "Compact EN v1");
    localized->MaterializeLanguage(1);
    CHECK(localized->Resolve(1) == "Compact RU v1");

    ui::UiLocalizationString::ptr::MakeFromThis(&*localized).Save();
    localized.Reset();
  }

  {
    ae::Domain cold_domain{boot.storage};
    auto cold = boot.LoadRoot(cold_domain);
    cold->MaterializeLanguage(0);
    CHECK(cold->Resolve(0) == "Compact EN v1");
    cold->MaterializeLanguage(1);
    CHECK(cold->Resolve(1) == "Compact RU v1");
    CHECK(cold->strings[0].id() == en_id);
    CHECK(cold->strings[1].id() == ru_id);

    CHECK(cold->TryReplayFromBase());
    cold->MaterializeLanguage(0);
    CHECK(cold->Resolve(0) == "Compact EN v1");
    cold->MaterializeLanguage(1);
    CHECK(cold->Resolve(1) == "Compact RU v1");

    CommitSlotUtf8(*cold, cold_domain, 0, "Compact EN v2");
    cold->MaterializeLanguage(1);
    CHECK(cold->Resolve(1) == "Compact RU v1");
    ui::UiLocalizationString::ptr::MakeFromThis(&*cold).Save();
    cold.Reset();
  }

  {
    ae::Domain reopened{boot.storage};
    auto again = boot.LoadRoot(reopened);
    again->MaterializeLanguage(0);
    CHECK(again->Resolve(0) == "Compact EN v2");
    again->MaterializeLanguage(1);
    CHECK(again->Resolve(1) == "Compact RU v1");
    again.Reset();
  }
}

class LocalizationCompactionScratchProbe
    : public NodeFor<LocalizationCompactionScratchProbe, UiLocalizationString> {};

static_assert(
    !std::is_same_v<UiLocalizationString::CompactionOperationScratch,
                    Node::CompactionOperationScratch>);
static_assert(
    std::is_same_v<LocalizationCompactionScratchProbe::CompactionOperationScratch,
                   UiLocalizationString::CompactionOperationScratch>);
static_assert(
    std::is_same_v<apptraverse::test::DerivedUiLocalizationString::
                       CompactionOperationScratch,
                   UiLocalizationString::CompactionOperationScratch>);
static_assert(
    sizeof(apptraverse::test::DerivedUiLocalizationString::
               CompactionOperationScratch) ==
    sizeof(UiLocalizationString::CompactionOperationScratch));

void TestDerivedUiLocalizationCompactionScratchInheritance() {
  ae::RamDomainStorage storage_full;
  ae::RamDomainStorage storage_partial;
  ae::ObjId const root_id_full{501};
  ae::ObjId const root_id_partial{502};
  char const* const kEnBootstrap = "Derived EN base";
  char const* const kRuBootstrap = "Derived RU base";
  ae::ObjId en_string_id;
  ae::ObjId ru_string_id;

  auto bootstrap = [&](ae::RamDomainStorage& ram_storage, ae::ObjId root_id) {
    ae::Domain domain{ram_storage};
    auto localized = apptraverse::test::DerivedUiLocalizationString::ptr::Create(
        ae::CreateWith{domain}.with_id(root_id));
    InitializeRuntimeNode(*localized);

    auto en = UiString::ptr::Create(ae::CreateWith{domain});
    en->utf8 = kEnBootstrap;
    auto ru = UiString::ptr::Create(ae::CreateWith{domain});
    ru->utf8 = kRuBootstrap;
    en_string_id = en->obj_id;
    ru_string_id = ru->obj_id;
    en.Save();
    ru.Save();

    localized->strings = {
        UiString::ptr::Declare(
            ae::CreateWith{domain}
                .with_id(en_string_id)
                .with_flags(ae::ObjFlags::kUnloadedByDefault)),
        UiString::ptr::Declare(
            ae::CreateWith{domain}
                .with_id(ru_string_id)
                .with_flags(ae::ObjFlags::kUnloadedByDefault)),
    };
    apptraverse::test::DerivedUiLocalizationString::ptr::MakeFromThis(
        &*localized)
        .Save();
  };

  bootstrap(storage_full, root_id_full);
  bootstrap(storage_partial, root_id_partial);

  ae::ObjId en_slot_id;
  ae::ObjId ru_slot_id;
  {
    ae::Domain domain{storage_full};
    auto localized =
        apptraverse::test::DerivedUiLocalizationString::ptr::Declare(
            ae::CreateWith{domain}.with_id(root_id_full));
    localized.Load();
    localized->CaptureBaseState();
    localized->MaterializeLanguage(0);
    CommitSlotUtf8(*localized, domain, 0, "Derived EN v1");
    CommitSlotUtf8(*localized, domain, 1, "Derived RU v1");
    CommitSlotUtf8(*localized, domain, 0, "Derived EN v2");
    en_slot_id = localized->strings[0].id();
    ru_slot_id = localized->strings[1].id();

    localized->SetJournalRetentionPolicy(JournalRetentionPolicy{.max_events = 0});
    localized->CompactJournal(SystemUtcMicros());
    CHECK(localized->journal.empty());
    localized->MaterializeLanguage(0);
    CHECK(localized->Resolve(0) == "Derived EN v2");
    localized->MaterializeLanguage(1);
    CHECK(localized->Resolve(1) == "Derived RU v1");
    CHECK(localized->strings[0].id() == en_slot_id);
    CHECK(localized->strings[1].id() == ru_slot_id);
    apptraverse::test::DerivedUiLocalizationString::ptr::MakeFromThis(
        &*localized)
        .Save();
    localized.Reset();
  }

  {
    ae::Domain cold_domain{storage_full};
    auto cold = apptraverse::test::DerivedUiLocalizationString::ptr::Declare(
        ae::CreateWith{cold_domain}.with_id(root_id_full));
    cold.Load();
    CHECK(cold->TryReplayFromBase());
    cold->MaterializeLanguage(0);
    CHECK(cold->Resolve(0) == "Derived EN v2");
    cold->MaterializeLanguage(1);
    CHECK(cold->Resolve(1) == "Derived RU v1");
    cold.Reset();
  }

  ae::ObjId ru_slot_id_partial;
  {
    ae::Domain domain{storage_partial};
    auto localized =
        apptraverse::test::DerivedUiLocalizationString::ptr::Declare(
            ae::CreateWith{domain}.with_id(root_id_partial));
    localized.Load();
    localized->CaptureBaseState();
    localized->MaterializeLanguage(0);
    CommitSlotUtf8(*localized, domain, 0, "Partial EN v1");
    CommitSlotUtf8(*localized, domain, 1, "Partial RU v1");
    CommitSlotUtf8(*localized, domain, 0, "Partial EN v2");
    ru_slot_id_partial = localized->strings[1].id();
    localized->MaterializeLanguage(0);

    localized->SetJournalRetentionPolicy(JournalRetentionPolicy{.max_events = 1});
    localized->CompactJournal(SystemUtcMicros());
    CHECK(localized->journal.size() == 1);
    localized->MaterializeLanguage(1);
    CHECK(localized->Resolve(1) == "Partial RU v1");
    apptraverse::test::DerivedUiLocalizationString::ptr::MakeFromThis(
        &*localized)
        .Save();
    localized.Reset();
  }

  {
    ae::Domain cold_domain{storage_partial};
    auto cold = apptraverse::test::DerivedUiLocalizationString::ptr::Declare(
        ae::CreateWith{cold_domain}.with_id(root_id_partial));
    cold.Load();
    CHECK(cold->TryReplayFromBase());
    cold->MaterializeLanguage(0);
    CHECK(cold->Resolve(0) == "Partial EN v2");
    cold->MaterializeLanguage(1);
    CHECK(cold->Resolve(1) == "Partial RU v1");
    CHECK(cold->strings[1].id() == ru_slot_id_partial);
    CHECK(cold->base.is_valid() && cold->base.is_loaded());
    CHECK(cold->base.id() != cold->obj_id);
  }
}

void TestLocalizationHistoricalCaptureScratchIsolationTwoOwners() {
  LazyLocalizationBootstrap boot_a{26, "IsoA EN", "IsoA RU"};
  LazyLocalizationBootstrap boot_b{27, "IsoB EN", "IsoB RU"};
  ae::ObjId a_en_id;
  ae::ObjId b_en_id;
  {
    ae::Domain domain_a{boot_a.storage};
    ae::Domain domain_b{boot_b.storage};
    auto localized_a = boot_a.LoadRoot(domain_a);
    auto localized_b = boot_b.LoadRoot(domain_b);
    localized_a->CaptureBaseState();
    localized_b->CaptureBaseState();
    localized_a->MaterializeLanguage(0);
    localized_b->MaterializeLanguage(0);
    CommitSlotUtf8(*localized_a, domain_a, 0, "IsoA EN live");
    CommitSlotUtf8(*localized_b, domain_b, 0, "IsoB EN live");
    CommitSlotUtf8(*localized_a, domain_a, 1, "IsoA RU live");
    CommitSlotUtf8(*localized_b, domain_b, 1, "IsoB RU live");
    a_en_id = localized_a->strings[0].id();
    b_en_id = localized_b->strings[0].id();

    ui::UiLocalizationString::RebuildOperationScratch rebuild_a{};
    ui::UiLocalizationString::RebuildOperationScratch rebuild_b{};
    auto const strings_a = localized_a->strings;
    auto const strings_b = localized_b->strings;
    localized_a->StashLocalPersistentAcrossRebuild(&rebuild_a);
    localized_a->strings = {};
    localized_b->StashLocalPersistentAcrossRebuild(&rebuild_b);
    localized_b->strings = {};
    localized_a->RestoreLocalPersistentAcrossRebuild(&rebuild_a);
    localized_b->RestoreLocalPersistentAcrossRebuild(&rebuild_b);
    CHECK(localized_a->strings.size() == strings_a.size());
    CHECK(localized_b->strings.size() == strings_b.size());
    CHECK(localized_a->strings[0].id() == a_en_id);
    CHECK(localized_b->strings[0].id() == b_en_id);

    ui::UiLocalizationString::CompactionOperationScratch compaction_a{};
    std::size_t const collapse_a = localized_a->journal.size();
    localized_a->FoldCollapsedJournalPrefixIntoReachableStorage(collapse_a,
                                                                &compaction_a);
    localized_a->StashLiveReachableLayersBeforeHistoricalCapture(&compaction_a);

    localized_b->SetJournalRetentionPolicy(JournalRetentionPolicy{.max_events = 0});
    localized_b->CompactJournal(SystemUtcMicros());
    CHECK(localized_b->journal.empty());
    localized_b->MaterializeLanguage(0);
    CHECK(localized_b->Resolve(0) == "IsoB EN live");

    localized_a->journal.clear();
    localized_a->CaptureBaseState();
    localized_a->ReleaseFoldCaptureMaterialization(&compaction_a);
    localized_a->RestoreLiveReachableLayersAfterHistoricalCapture(&compaction_a);

    localized_a->MaterializeLanguage(0);
    CHECK(localized_a->Resolve(0) == "IsoA EN live");
    localized_a->MaterializeLanguage(1);
    CHECK(localized_a->Resolve(1) == "IsoA RU live");
    localized_b->MaterializeLanguage(1);
    CHECK(localized_b->Resolve(1) == "IsoB RU live");
    CHECK(localized_a->strings[0].id() == a_en_id);
    CHECK(localized_b->strings[0].id() == b_en_id);

    CHECK(localized_a->TryReplayFromBase());
    CHECK(localized_b->TryReplayFromBase());
    localized_a->MaterializeLanguage(0);
    localized_b->MaterializeLanguage(0);
    CHECK(localized_a->Resolve(0) == "IsoA EN live");
    CHECK(localized_b->Resolve(0) == "IsoB EN live");
  }
}

void TestLocalizationJournalPartialCompactionRetainsOtherSlot() {
  LazyLocalizationBootstrap boot{24, "Partial EN", "Partial RU"};
  ae::ObjId ru_id;
  {
    ae::Domain domain{boot.storage};
    auto localized = boot.LoadRoot(domain);
    localized->CaptureBaseState();
    localized->MaterializeLanguage(0);
    CommitSlotUtf8(*localized, domain, 0, "Partial EN v1");
    CommitSlotUtf8(*localized, domain, 1, "Partial RU v1");
    CommitSlotUtf8(*localized, domain, 0, "Partial EN v2");
    CHECK(!localized->strings[1].is_loaded());
    CHECK(localized->journal.size() >= 3);

    ru_id = localized->strings[1].id();
    localized->MaterializeLanguage(0);
    CHECK(!localized->strings[1].is_loaded());

    localized->SetJournalRetentionPolicy(JournalRetentionPolicy{.max_events = 1});
    localized->CompactJournal(SystemUtcMicros());
    CHECK(localized->journal.size() == 1);
    CHECK(CountLocalizationUtf8Events(*localized) == 1);
    auto const& retained = localized->journal.front().event;
    CHECK(retained.is_valid() && retained.is_loaded());
    auto const& utf8_event =
        static_cast<SetUiLocalizationSlotUtf8Event const&>(*retained);
    CHECK(utf8_event.slot_index == 0);
    CHECK(utf8_event.utf8 == "Partial EN v2");

    localized->MaterializeLanguage(1);
    CHECK(localized->Resolve(1) == "Partial RU v1");

    ui::UiLocalizationString::ptr::MakeFromThis(&*localized).Save();
    localized.Reset();
  }

  {
    ae::Domain cold_domain{boot.storage};
    auto cold = boot.LoadRoot(cold_domain);
    CHECK(cold->TryReplayFromBase());
    CHECK(CountLocalizationUtf8Events(*cold) == 1);
    cold->MaterializeLanguage(1);
    CHECK(cold->Resolve(1) == "Partial RU v1");
    CHECK(cold->strings[1].id() == ru_id);
    cold->MaterializeLanguage(0);
    CHECK(cold->Resolve(0) == "Partial EN v2");
    cold.Reset();
  }
}

void TestColdLoadStoredClassFactoryContract() {
  {
    LazyLocalizationBootstrap boot{6101, "FactoryContract EN", "FactoryContract RU"};
    ae::Domain domain{boot.storage};
    auto loaded = boot.LoadRoot(domain);
    CHECK(loaded->GetClassId() == ui::UiLocalizationString::kClassId);
    CHECK(loaded->GetClassId() !=
          apptraverse::test::DerivedUiLocalizationString::kClassId);
    loaded->MaterializeLanguage(0);
    CHECK(loaded->Resolve(0) == "FactoryContract EN");
  }

  ae::RamDomainStorage derived_storage;
  ae::ObjId const derived_root{6102};
  {
    ae::Domain domain{derived_storage};
    auto localized =
        apptraverse::test::DerivedUiLocalizationString::ptr::Create(
            ae::CreateWith{domain}.with_id(derived_root));
    InitializeRuntimeNode(*localized);
    auto en = ui::UiString::ptr::Create(ae::CreateWith{domain});
    en->utf8 = "Derived factory contract";
    en.Save();
    localized->strings = {ui::UiString::ptr::Declare(
        ae::CreateWith{domain}
            .with_id(en->obj_id)
            .with_flags(ae::ObjFlags::kUnloadedByDefault))};
    apptraverse::test::DerivedUiLocalizationString::ptr::MakeFromThis(
        &*localized)
        .Save();
  }
  {
    ae::Domain cold{derived_storage};
    auto loaded =
        apptraverse::test::DerivedUiLocalizationString::ptr::Declare(
            ae::CreateWith{cold}.with_id(derived_root));
    loaded.Load();
    CHECK(loaded->GetClassId() ==
          apptraverse::test::DerivedUiLocalizationString::kClassId);
    loaded->MaterializeLanguage(0);
    CHECK(loaded->Resolve(0) == "Derived factory contract");
  }

  constexpr std::uint32_t kUnknownStoredClass = 0xC1D00002u;
  CHECK(!ae::Registry::GetRegistry().IsExisting(kUnknownStoredClass));
  LazyLocalizationBootstrap tainted{6103, "Reject unknown EN", "Reject unknown RU"};
  {
    auto& obj_layers = tainted.storage.state[tainted.root_id];
    CHECK(obj_layers.has_value());
    auto const& loc_v0 =
        (*obj_layers)[ui::UiLocalizationString::kClassId][0];
    tainted.storage.SaveData(
        ae::DomainQuery{tainted.root_id, kUnknownStoredClass, 0},
        ae::ObjectData{loc_v0});
  }
  {
    ae::Domain domain{tainted.storage};
    ae::DomainGraph graph{&domain};
    ae::Ptr<ae::Obj> loaded;
    CHECK(graph.LoadRoot(tainted.root_id, loaded).IsErr());
  }
}

void TestLocalizationHistoricalBaseIndependentOfOwnerSaveOnDisk() {
  auto const dir = LocalizationTestDir("historical_base_ru_slot");
  ae::ObjId const root_id{25};
  char const* const kRuA = "A";
  char const* const kRuB = "B";
  char const* const kRuC = "C";
  char const* const kRuD = "D";
  ae::ObjId en_string_id;
  ae::ObjId ru_string_id;

  {
    DirectoryDomainStorage storage{dir};
    ae::Domain domain{storage};
    auto localized = ui::UiLocalizationString::ptr::Create(
        ae::CreateWith{domain}.with_id(root_id));
    InitializeRuntimeNode(*localized);

    auto en = ui::UiString::ptr::Create(ae::CreateWith{domain});
    en->utf8 = "EN-active";
    auto ru = ui::UiString::ptr::Create(ae::CreateWith{domain});
    ru->utf8 = kRuA;
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
    localized->CaptureBaseState();
    storage.BeginSaveSession();
    ui::UiLocalizationString::ptr::MakeFromThis(&*localized).Save();
    CHECK(!storage.SaveSessionHadFailure());
  }
  ae::ObjId bootstrap_base_id;
  {
    DirectoryDomainStorage storage{dir};
    ae::Domain domain{storage};
    auto localized = ui::UiLocalizationString::ptr::Declare(
        ae::CreateWith{domain}.with_id(root_id));
    localized.Load();
    bootstrap_base_id = localized->base.id();
    auto snap = ui::UiLocalizationString::ptr::Declare(
        ae::CreateWith{domain}.with_id(bootstrap_base_id));
    snap.Load();
    CHECK(snap->strings.size() == 2);
  }

  ae::ObjId base_id;
  ae::ObjId ru_ref_id;
  {
    DirectoryDomainStorage storage{dir};
    ae::Domain domain{storage};
    auto localized = ui::UiLocalizationString::ptr::Declare(
        ae::CreateWith{domain}.with_id(root_id));
    localized.Load();
    localized->MaterializeLanguage(0);
    CHECK(!localized->strings[1].is_loaded());
    CHECK(Utf8FromDirectoryStorage(storage, ru_string_id) == kRuA);

    CommitSlotUtf8(*localized, domain, 1, kRuB);
    CommitSlotUtf8(*localized, domain, 1, kRuC);
    CHECK(!localized->strings[1].is_loaded());
    CHECK(Utf8FromDirectoryStorage(storage, ru_string_id) == kRuA);
    CHECK(localized->journal.size() == 2);

    localized->MaterializeLanguage(0);
    CHECK(!localized->strings[1].is_loaded());

    localized->SetJournalRetentionPolicy(JournalRetentionPolicy{.max_events = 1});
    localized->CompactJournal(SystemUtcMicros());
    CHECK(localized->journal.size() == 1);
    CHECK(CountLocalizationUtf8Events(*localized) == 1);
    auto const& retained = localized->journal.front().event;
    CHECK(retained.is_valid() && retained.is_loaded());
    auto const& utf8_event =
        static_cast<SetUiLocalizationSlotUtf8Event const&>(*retained);
    CHECK(utf8_event.slot_index == 1);
    CHECK(utf8_event.utf8 == kRuC);

    base_id = localized->base.id();
    ru_ref_id = localized->strings[1].id();
    CHECK(base_id != root_id);
    CHECK(ru_ref_id == ru_string_id);

    localized->MaterializeLanguage(1);
    CHECK(localized->Resolve(1) == kRuC);

    storage.BeginSaveSession();
    ui::UiLocalizationString::ptr::MakeFromThis(&*localized).Save();
    CHECK(!storage.SaveSessionHadFailure());
    localized.Reset();
  }
  {
    DirectoryDomainStorage storage{dir};
    ae::Domain domain{storage};
    auto historical = ui::UiLocalizationString::ptr::Declare(
        ae::CreateWith{domain}.with_id(base_id));
    historical.Load();
    CHECK(historical->journal.empty());
    CHECK(historical->strings.size() == 2);
    CHECK(historical->strings[1].is_valid());
    historical->MaterializeLanguage(1);
    CHECK(historical->Resolve(1) == kRuB);
    CHECK(historical->strings[1].id() != ru_ref_id);
    CHECK(historical->strings[1].is_valid());
    historical.Reset();
  }

  {
    DirectoryDomainStorage storage{dir};
    ae::Domain domain{storage};
    auto loaded = ui::UiLocalizationString::ptr::Declare(
        ae::CreateWith{domain}.with_id(root_id));
    loaded.Load();
    loaded->MaterializeLanguage(1);
    CHECK(loaded->Resolve(1) == kRuC);
    CHECK(loaded->strings[1].id() == ru_ref_id);
    CHECK(loaded->base.id() == base_id);
    loaded.Reset();
  }

  {
    DirectoryDomainStorage storage{dir};
    ae::Domain domain{storage};
    auto replay = ui::UiLocalizationString::ptr::Declare(
        ae::CreateWith{domain}.with_id(root_id));
    replay.Load();
    CHECK(replay->TryReplayFromBase());
    replay->MaterializeLanguage(1);
    CHECK(replay->Resolve(1) == kRuC);
    CHECK(replay->strings[1].id() == ru_ref_id);
    CHECK(replay->base.id() == base_id);
    replay.Reset();
  }

  {
    DirectoryDomainStorage storage{dir};
    ae::Domain domain{storage};
    auto localized = ui::UiLocalizationString::ptr::Declare(
        ae::CreateWith{domain}.with_id(root_id));
    localized.Load();
    CommitSlotUtf8(*localized, domain, 1, kRuD);
    localized->MaterializeLanguage(1);
    CHECK(localized->Resolve(1) == kRuD);
    storage.BeginSaveSession();
    ui::UiLocalizationString::ptr::MakeFromThis(&*localized).Save();
    CHECK(!storage.SaveSessionHadFailure());
    localized.Reset();
  }

  {
    DirectoryDomainStorage storage{dir};
    ae::Domain domain{storage};
    auto historical = ui::UiLocalizationString::ptr::Declare(
        ae::CreateWith{domain}.with_id(base_id));
    historical.Load();
    historical->MaterializeLanguage(1);
    CHECK(historical->Resolve(1) == kRuB);
    historical.Reset();
  }

  {
    DirectoryDomainStorage storage{dir};
    ae::Domain domain{storage};
    auto loaded = ui::UiLocalizationString::ptr::Declare(
        ae::CreateWith{domain}.with_id(root_id));
    loaded.Load();
    loaded->MaterializeLanguage(1);
    CHECK(loaded->Resolve(1) == kRuD);
    loaded.Reset();
  }

  {
    DirectoryDomainStorage storage{dir};
    ae::Domain domain{storage};
    auto replay = ui::UiLocalizationString::ptr::Declare(
        ae::CreateWith{domain}.with_id(root_id));
    replay.Load();
    CHECK(replay->TryReplayFromBase());
    replay->MaterializeLanguage(1);
    CHECK(replay->Resolve(1) == kRuD);
    replay.Reset();
  }
}

void TestLocalizationApplyUnloadedSlotSkipsStorage() {
  ae::RamDomainStorage storage;
  ae::ObjId const root_id{12};
  ae::ObjId en_string_id;
  ae::ObjId ru_string_id;
  char const* const kRuEdited = "RU from Apply";

  {
    ae::Domain domain{storage};
    auto localized = ui::UiLocalizationString::ptr::Create(
        ae::CreateWith{domain}.with_id(root_id));
    InitializeRuntimeNode(*localized);

    auto en = ui::UiString::ptr::Create(ae::CreateWith{domain});
    en->utf8 = "EN";
    auto ru = ui::UiString::ptr::Create(ae::CreateWith{domain});
    ru->utf8 = "RU";
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
  auto localized = ui::UiLocalizationString::ptr::Declare(
      ae::CreateWith{domain}.with_id(root_id));
  localized.Load();
  localized->MaterializeLanguage(0);
  CHECK(localized->strings[0].is_loaded());
  CHECK(!localized->strings[1].is_loaded());
  std::size_t const ru_layer_count_bootstrap =
      storage.Enumerate(ru_string_id).size();
  std::string const ru_utf8_bootstrap =
      Utf8FromStorageOnly(storage, ru_string_id);

  auto edit = SetUiLocalizationSlotUtf8Event::ptr::Create(ae::CreateWith{domain});
  edit->slot_index = 1;
  edit->utf8 = kRuEdited;
  localized->Commit(std::move(edit));
  CHECK(!localized->strings[1].is_loaded());
  CHECK(storage.Enumerate(ru_string_id).size() == ru_layer_count_bootstrap);
  CHECK(Utf8FromStorageOnly(storage, ru_string_id) == ru_utf8_bootstrap);
  CHECK(!domain.Find(ru_string_id));

  localized->MaterializeLanguage(1);
  CHECK(localized->Resolve(1) == kRuEdited);
  CHECK(storage.Enumerate(ru_string_id).size() == ru_layer_count_bootstrap);
  CHECK(Utf8FromStorageOnly(storage, ru_string_id) == ru_utf8_bootstrap);
}

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

void TestListSelectionViaPresenterProxy() {
  ae::RamDomainStorage model_storage;
  ae::RamDomainStorage ui_storage;
  ae::Domain model_domain{model_storage};
  ae::Domain ui_domain{ui_storage};

  auto doc = UiWindow::ptr::Create(ae::CreateWith{model_domain});
  doc->BootstrapClientSize(800, 600);
  InitializeRuntimeNode(*doc);
  auto list = UiListContainer::ptr::Create(ae::CreateWith{model_domain});
  list->selection_index = -1;
  list->selection_index = -1;
  InitializeRuntimeNode(*list, *doc);
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
  auto ui_obj = LoadInitialPublication(initial_in, ui_domain, ui_storage);
  CHECK(ui_obj);
  auto ui_list = UiListContainer::ptr::MakeFromThis(
      static_cast<UiListContainer*>(ui_obj.get()));
  CHECK(ui_list->selection_index == -1);

  auto ui_window_presenter = UiWindowPresenter::ptr::Create(
      ae::CreateWith{ui_domain});
  ui_window_presenter->presentation_loaded = true;
  auto ui_list_presenter = UiListContainerPresenter::ptr::Create(
      ae::CreateWith{ui_domain});
  ui_list_presenter->container = ui_list;
  ui_list_presenter->window_presenter = ui_window_presenter;
  ui_list_presenter->model_proxy = &proxy;

  int materialize_notify = 0;
  list->BindMaterializedChangeNotifier(&materialize_notify, &PendingNotify);

  ui_list_presenter->SubmitListRowSelectionChangedFromUi(1);
  CHECK(!deferred.empty());
  CHECK(list->selection_index == -1);
  CHECK(ui_list->selection_index == -1);
  CHECK(materialize_notify == 0);
  CHECK(CountJournalEventsWithClass(*list, SetUiListSelectionEvent::kClassId) ==
        0);

  PumpModelWork(deferred, model_domain);

  CHECK(list->selection_index == 1);
  CHECK(materialize_notify == 1);
  CHECK(CountJournalEventsWithClass(*list,
                                    SetUiListSelectionEvent::kClassId) == 1);
  CHECK(ui_list->selection_index == -1);

  ByteSink structural;
  SerializeStructuralNodePublication(*list, structural);
  ByteSource structural_in;
  structural_in.data = structural.bytes.data();
  structural_in.size = structural.bytes.size();
  ApplyStructuralPublication(structural_in, ui_domain, ui_storage);

  CHECK(ui_list->selection_index == 1);

  ui_list_presenter->SubmitListRowSelectionChangedFromUi(1);
  PumpModelWork(deferred, model_domain);
  CHECK(CountJournalEventsWithClass(*list,
                                    SetUiListSelectionEvent::kClassId) == 1);
  CHECK(materialize_notify == 1);

  list->ClearMaterializedChangeNotifier();
}

std::vector<ae::ObjId> RowIds(UiListContainer const& list) {
  std::vector<ae::ObjId> ids;
  for (auto const& row : list.rows) {
    ids.push_back(row.id());
  }
  return ids;
}

void EnqueueRemoveRow(ModelObjectProxy& proxy, ae::ObjId list_id,
                      ae::ObjId row_id) {
  proxy.EnqueueModelWork([list_id, row_id](ae::Domain& domain) {
    auto remove = RemoveUiListRowEvent::ptr::Create(ae::CreateWith{domain});
    remove->row_id = row_id;
    domain.Find(list_id).as<UiListContainer>()->Commit(std::move(remove));
  });
}

void TestListRemoveRowByReferenceIdentity() {
  ae::RamDomainStorage model_storage;
  ae::RamDomainStorage ui_storage;
  ae::Domain model_domain{model_storage};
  ae::Domain ui_domain{ui_storage};

  auto list = UiListContainer::ptr::Create(ae::CreateWith{model_domain});
  list->selection_index = -1;
  list->selection_index = -1;
  InitializeRuntimeNode(*list);
  std::vector<UiListRow::ptr> held;
  for (int i = 0; i < 4; ++i) {
    auto row = UiListRow::ptr::Create(ae::CreateWith{model_domain});
    InitializeRuntimeNode(*row, *list);
    held.push_back(row);
  }
  list->rows = {held[0], held[1], held[2], held[3]};

  ByteSink initial;
  SerializeInitialPublication(*list, initial);
  ByteSource initial_in;
  initial_in.data = initial.bytes.data();
  initial_in.size = initial.bytes.size();
  auto ui_obj = LoadInitialPublication(initial_in, ui_domain, ui_storage);
  CHECK(ui_obj);
  auto ui_list = UiListContainer::ptr::MakeFromThis(
      static_cast<UiListContainer*>(ui_obj.get()));
  CHECK(RowIds(*ui_list) == RowIds(*list));

  std::deque<ModelObjectProxy::ModelWork> deferred;
  ModelObjectProxy proxy{[&](ModelObjectProxy::ModelWork work) {
    deferred.push_back(std::move(work));
  }};
  int materialize_notify = 0;
  list->BindMaterializedChangeNotifier(&materialize_notify, &PendingNotify);

  // Reset() drops the cached body and keeps the ObjId: this checks the
  // reference-identity contract, not a natural cold-load path.
  list->rows[1].Reset();
  list->rows[3].Reset();
  CHECK(list->rows[1].is_valid() && !list->rows[1].is_loaded());
  EnqueueRemoveRow(proxy, list->obj_id, held[1]->obj_id);
  PumpModelWork(deferred, model_domain);
  CHECK((RowIds(*list) ==
         std::vector<ae::ObjId>{held[0]->obj_id, held[2]->obj_id,
                                held[3]->obj_id}));
  CHECK(!list->rows[2].is_loaded());
  CHECK(materialize_notify == 1);

  EnqueueRemoveRow(proxy, list->obj_id, held[2]->obj_id);
  PumpModelWork(deferred, model_domain);
  CHECK((RowIds(*list) ==
         std::vector<ae::ObjId>{held[0]->obj_id, held[3]->obj_id}));
  CHECK(list->rows[0].is_loaded());
  CHECK(!list->rows[1].is_loaded());
  CHECK(materialize_notify == 2);

  EnqueueRemoveRow(proxy, list->obj_id, held[2]->obj_id);
  PumpModelWork(deferred, model_domain);
  CHECK(list->rows.size() == 2);
  CHECK(materialize_notify == 2);
  CHECK(list->selection_index == -1);

  CHECK(ui_list->rows.size() == 4);
  ByteSink structural;
  SerializeStructuralNodePublication(*list, structural);
  ByteSource structural_in;
  structural_in.data = structural.bytes.data();
  structural_in.size = structural.bytes.size();
  ApplyStructuralPublication(structural_in, ui_domain, ui_storage);
  CHECK(RowIds(*ui_list) == RowIds(*list));
  CHECK(ui_list->selection_index == -1);
  CHECK(deferred.empty());

  list->ClearMaterializedChangeNotifier();
}

ui::UiLocalizationString::ptr MakeRowLabel(ae::Domain& domain,
                                           char const* utf8) {
  auto label = ui::UiLocalizationString::ptr::Create(ae::CreateWith{domain});
  auto str = ui::UiString::ptr::Create(ae::CreateWith{domain});
  str->utf8 = utf8;
  label->strings = {str};
  return label;
}

void TestPresentedListRowPrimaryTextsMatchRowIndices() {
  ae::RamDomainStorage model_storage;
  ae::Domain model_domain{model_storage};
  auto list = UiListContainer::ptr::Create(ae::CreateWith{model_domain});
  list->selection_index = -1;
  list->selection_index = -1;
  auto row0 = UiListRow::ptr::Create(ae::CreateWith{model_domain});
  auto row1 = UiListRow::ptr::Create(ae::CreateWith{model_domain});
  row0->primary_text = MakeRowLabel(model_domain, "Row A");
  row1->primary_text = MakeRowLabel(model_domain, "Row B");
  list->rows = {row0, row1};

  ae::RamDomainStorage ui_storage;
  ae::Domain ui_domain{ui_storage};
  ByteSink initial;
  SerializeInitialPublication(*list, initial);
  ByteSource initial_in;
  initial_in.data = initial.bytes.data();
  initial_in.size = initial.bytes.size();
  auto ui_obj = LoadInitialPublication(initial_in, ui_domain, ui_storage);
  CHECK(ui_obj);
  auto ui_list = UiListContainer::ptr::MakeFromThis(
      static_cast<UiListContainer*>(ui_obj.get()));
  CHECK(ui_list->rows.size() == 2);

  std::vector<std::string> const labels =
      ResolvePresentedListRowPrimaryTexts(*ui_list, 0);
  CHECK(labels.size() == ui_list->rows.size());
  CHECK(labels[0] == "Row A");
  CHECK(labels[1] == "Row B");

  // Reset() drops cached bodies but keeps ObjIds — reference-contract check,
  // not a natural cold-load path. Resolver must still return the full list.
  for (auto& row : ui_list->rows) {
    row.Load();
    row->primary_text.Reset();
    CHECK(row->primary_text.is_valid());
    CHECK(!row->primary_text.is_loaded());
  }
  std::vector<std::string> const labels_unloaded =
      ResolvePresentedListRowPrimaryTexts(*ui_list, 0);
  CHECK(labels_unloaded.size() == ui_list->rows.size());
  CHECK(labels_unloaded[0] == "Row A");
  CHECK(labels_unloaded[1] == "Row B");
}

void TestPushButtonClickViaPresenter() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto doc = UiWindow::ptr::Create(ae::CreateWith{domain});
  doc->BootstrapClientSize(800, 600);
  InitializeRuntimeNode(*doc);
  auto button = TestClickButton::ptr::Create(ae::CreateWith{domain});
  button->BootstrapSetEnabled(true);
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

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  EnableNoninteractiveCrt();
  EnsureObjectRegistration();
  apptraverse::ui::EnsureUiPresenterRegistration();
  EnsureTestRegistration();

  TestExplicitEmptyLocalization();
  TestImmediateCompactionKeepsInactiveLanguageLazy();
  TestPushButtonMaterializationNotifiesOnChange();
  TestLocalizationLazyLoadRoundtrip();
  TestLocalizationLazyInitialPublication();
  TestLocalizationInactiveSlotAbaWithoutPersist();
  TestLocalizationSequentialSlotEditsLastWins();
  TestLocalizationLanguageSwitchWithoutImplicitPersist();
  TestLocalizationDirectoryLifecycleWithoutSessionWrites();
  TestLocalizationJournalCompactionMatchesLoad();
  TestLocalizationHistoricalCaptureScratchIsolationTwoOwners();
  TestLocalizationJournalPartialCompactionRetainsOtherSlot();
  TestDerivedUiLocalizationCompactionScratchInheritance();
  TestColdLoadStoredClassFactoryContract();
  TestLocalizationHistoricalBaseIndependentOfOwnerSaveOnDisk();
  TestLocalizationApplyUnloadedSlotSkipsStorage();
  TestPushButtonClickViaPresenter();
  TestListSelectionViaPresenterProxy();
  TestListRemoveRowByReferenceIdentity();
  TestPresentedListRowPrimaryTextsMatchRowIndices();
  return 0;
}
