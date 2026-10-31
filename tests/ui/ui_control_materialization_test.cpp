#include <cstring>
#include <deque>
#include <filesystem>
#include <iostream>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#ifdef RegisterClass
#undef RegisterClass
#endif
#endif

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"

#include "apptraverse/distill.h"
#include "apptraverse/model_object_proxy.h"
#include "apptraverse/object_serialization.h"
#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/runtime_node.h"
#include "apptraverse/node.h"
#include "apptraverse/ui/ui_edit_box.h"
#include "apptraverse/ui/ui_list_container.h"
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
  InitializeRuntimeNode(*doc);
  auto list = UiListContainer::ptr::Create(ae::CreateWith{model_domain});
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
}

#ifdef _WIN32
std::filesystem::path MaterializationTestExePath() {
  wchar_t buf[MAX_PATH];
  DWORD const n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
  CHECK(n > 0 && n < MAX_PATH);
  return std::filesystem::path(buf);
}

std::string ReadAllBytes(std::filesystem::path const& path) {
  HANDLE file = CreateFileW(path.wstring().c_str(), GENERIC_READ,
                            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  CHECK(file != INVALID_HANDLE_VALUE);
  DWORD const size = GetFileSize(file, nullptr);
  CHECK(size != INVALID_FILE_SIZE);
  std::string out(size, '\0');
  DWORD read = 0;
  CHECK(ReadFile(file, out.data(), size, &read, nullptr) != 0);
  CloseHandle(file);
  out.resize(read);
  return out;
}

void RunMaterializationChildExpectingFatal(char const* expected_substring) {
  auto const err_path = std::filesystem::temp_directory_path() /
                        "apptraverse_presented_list_row_fatal.stderr.txt";
  std::error_code ec;
  std::filesystem::remove(err_path, ec);

  std::filesystem::path const exe = MaterializationTestExePath();
  std::wstring cmd = L"\"" + exe.wstring() +
                     L"\" missing_primary_text_fatal";
  std::vector<wchar_t> cmd_buf(cmd.begin(), cmd.end());
  cmd_buf.push_back(L'\0');

  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;
  HANDLE errf = CreateFileW(err_path.wstring().c_str(), GENERIC_WRITE,
                            FILE_SHARE_READ, &sa, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  CHECK(errf != INVALID_HANDLE_VALUE);
  HANDLE nul = CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                           OPEN_EXISTING, 0, nullptr);
  CHECK(nul != INVALID_HANDLE_VALUE);

  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = nul;
  si.hStdOutput = nul;
  si.hStdError = errf;
  PROCESS_INFORMATION pi{};
  BOOL const ok =
      CreateProcessW(exe.wstring().c_str(), cmd_buf.data(), nullptr, nullptr,
                     TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
  CloseHandle(errf);
  CloseHandle(nul);
  CHECK(ok);
  CloseHandle(pi.hThread);
  CHECK(WaitForSingleObject(pi.hProcess, 30000) == WAIT_OBJECT_0);
  DWORD code = 1;
  GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hProcess);
  CHECK(code != 0);
  std::string const err = ReadAllBytes(err_path);
  CHECK(err.find(expected_substring) != std::string::npos);
}

void RunMissingPrimaryTextFatalChildMode() {
  EnsureObjectRegistration();
  apptraverse::ui::EnsureUiPresenterRegistration();
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto unlabeled = UiListRow::ptr::Create(ae::CreateWith{domain});
  (void)ResolvePresentedListRowPrimaryText(unlabeled, 0);
}
#endif

void TestPresentedListRowMissingPrimaryTextRefusesPartialList() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto unlabeled = UiListRow::ptr::Create(ae::CreateWith{domain});
  CHECK(!unlabeled->primary_text.is_valid());
#ifdef _WIN32
  RunMaterializationChildExpectingFatal(
      "fatal: presented list row label: missing mandatory primary_text link");
#else
  (void)ResolvePresentedListRowPrimaryText;
#endif
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

int main(int argc, char** argv) {
  EnableNoninteractiveCrt();
#ifdef _WIN32
  if (argc >= 2 &&
      std::strcmp(argv[1], "missing_primary_text_fatal") == 0) {
    RunMissingPrimaryTextFatalChildMode();
    return 1;
  }
#endif
  EnsureObjectRegistration();
  apptraverse::ui::EnsureUiPresenterRegistration();
  apptraverse::ui::EnsureExtendedUiPresenterRegistration();
  EnsureTestRegistration();

  TestEditBoxMaterializationNotifiesOnChange();
  TestLocalizationLazyLoadRoundtrip();
  TestLocalizationLazyInitialPublication();
  TestPushButtonClickViaPresenter();
  TestListSelectionViaPresenterProxy();
  TestPresentedListRowPrimaryTextsMatchRowIndices();
  TestPresentedListRowMissingPrimaryTextRefusesPartialList();
  return 0;
}
