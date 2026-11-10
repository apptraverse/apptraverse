#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

#include "aether-objects/obj/domain.h"

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/distill.h"
#include "apptraverse/node.h"
#include "apptraverse/node_for.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/runtime_node.h"
#include "apptraverse/ui/ui_desktop_window.h"
#include "apptraverse/ui/ui_localization_string.h"
#include "apptraverse/ui/ui_locale_settings.h"
#include "apptraverse/ui/ui_presented_window_composition.h"
#include "apptraverse/ui/ui_presented_window_lifecycle.h"
#include "apptraverse/ui/ui_presenters.h"
#include "apptraverse/ui/ui_string.h"

namespace apptraverse::test {
namespace {

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"       \
                << __LINE__ << '\n';                                         \
      std::exit(2);                                                          \
    }                                                                        \
  } while (false)

constexpr char kBaselineTitleUtf8[] = "PresenterBranchBaseline";
constexpr char kProbeTitleUtf8[] = "PresenterBranchProbe-X9";

ae::ObjId const kRootId{0xC00B0000u};
ae::ObjId const kWindowId{0xC00B0001u};
ae::ObjId const kPresenterId{0xC00B0002u};
ae::ObjId const kLocaleId{0xC00B0003u};
ae::ObjId const kTitleId{0xC00B0004u};
ae::ObjId const kEnStringId{0xC00B0005u};
ae::ObjId const kRuStringId{0xC00B0006u};

class PresenterBranchUnloadRoot : public NodeFor<PresenterBranchUnloadRoot> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::PresenterBranchUnloadRoot",
                           PresenterBranchUnloadRoot, Node, 0)

 protected:
  PresenterBranchUnloadRoot() = default;

 public:
  explicit PresenterBranchUnloadRoot(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(presented_window), AE_MMBR(presented_open))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, presented_window, presented_open);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, presented_window, presented_open);
  }

  ui::UiDesktopWindow::ptr presented_window;
  bool presented_open{false};
};

APPTRAVERSE_REGISTER(PresenterBranchUnloadRoot);

void EnsurePresenterBranchUnloadRegistration() {
  (void)&g_apptraverse_registrar_PresenterBranchUnloadRoot;
}

ui::UiLocalizationString::ptr BuildTitleOnPresenterOnly(
    ae::Domain& domain, Node& presenter_owner) {
  auto en = ui::UiString::ptr::Create(ae::CreateWith{domain}.with_id(kEnStringId));
  en->utf8 = kBaselineTitleUtf8;
  auto ru = ui::UiString::ptr::Create(ae::CreateWith{domain}.with_id(kRuStringId));
  ru->utf8 = "PresenterBranchRU";
  en.SetFlags(ae::ObjFlags::kUnloadedByDefault);
  ru.SetFlags(ae::ObjFlags::kUnloadedByDefault);

  auto title = ui::UiLocalizationString::ptr::Create(
      ae::CreateWith{domain}.with_id(kTitleId));
  title->strings = {en, ru};
  InitializeRuntimeNode(*title, presenter_owner);
  return title;
}

void BootstrapUserProfile(std::filesystem::path const& user_dir) {
  std::error_code ec;
  std::filesystem::remove_all(user_dir, ec);
  std::filesystem::create_directories(user_dir, ec);

  DirectoryDomainStorage storage{user_dir};
  ae::Domain domain{storage};
  auto root = PresenterBranchUnloadRoot::ptr::Create(
      ae::CreateWith{domain}.with_id(kRootId));
  InitializeRuntimeNode(*root);

  auto presenter = ui::UiDesktopWindowPresenter::ptr::Create(
      ae::CreateWith{domain}.with_id(kPresenterId));
  auto locale = ui::UiLocaleSettings::ptr::Create(
      ae::CreateWith{domain}.with_id(kLocaleId));
  InitializeRuntimeNode(*locale, *root);

  auto title = BuildTitleOnPresenterOnly(domain, *root);

  root->presented_window = ui::UiDesktopWindow::ptr::Create(
      ae::CreateWith{domain}.with_id(kWindowId));
  ui::ComposePresentedWindow(root->presented_window, presenter, title, locale);
  InitializeRuntimeNode(*root->presented_window, *root);

  // The first Close must persist the complete graph, including objects that
  // have never been saved by cloning or historical-base capture.
  CHECK(std::filesystem::is_empty(user_dir));
  root->presented_open = false;
  ui::UnloadPresentedWindow(root->presented_window);

  FinalizeDistilledGraph(*root);
  SaveDistilledRoot(*root);
}

std::string ReadTitleViaPresenterOnly(PresenterBranchUnloadRoot& root) {
  root.presented_window.Load();
  CHECK(root.presented_window.is_valid());
  root.presented_window->presenter.Load();
  CHECK(root.presented_window->presenter.is_valid());
  auto& pres = static_cast<ui::UiDesktopWindowPresenter&>(
      *root.presented_window->presenter);
  pres.title.Load();
  CHECK(pres.title.is_valid());
  pres.title->strings[0].Load();
  CHECK(pres.title->strings[0].is_valid());
  return pres.title->strings[0]->utf8;
}

std::filesystem::path UserDir() {
  char const* suffix = std::getenv("APPTRAVERSE_UNLOAD_RT_USER_SUFFIX");
  auto dir = std::filesystem::temp_directory_path() /
             "unload_presented_window_presenter_branch_disk_roundtrip";
  if (suffix != nullptr && suffix[0] != '\0') {
    dir += suffix;
  }
  return dir;
}

void RunRoundTrip() {
  std::filesystem::path const user_dir = UserDir();
  BootstrapUserProfile(user_dir);

  {
    DirectoryDomainStorage storage{user_dir};
    ae::Domain domain{storage};
    auto root = LoadApplication<PresenterBranchUnloadRoot>(domain, kRootId);
    CHECK(!root->presented_open);
    ui::LoadPresentedWindow(root->presented_window);
    CHECK(ReadTitleViaPresenterOnly(*root) == kBaselineTitleUtf8);
  }

  ae::ObjId presenter_id{};
  ae::ObjId window_id{};

  {
    DirectoryDomainStorage storage{user_dir};
    ae::Domain domain{storage};
    auto root = LoadApplication<PresenterBranchUnloadRoot>(domain, kRootId);
    ui::LoadPresentedWindow(root->presented_window);
    CHECK(ReadTitleViaPresenterOnly(*root) == kBaselineTitleUtf8);

    window_id = root->presented_window.id();
    presenter_id = root->presented_window->presenter.id();
    CHECK(presenter_id.is_valid());

    {
      auto& pres = static_cast<ui::UiDesktopWindowPresenter&>(
          *root->presented_window->presenter);
      pres.title.Load();
      pres.title->strings[0].Load();
      pres.title->strings[0]->utf8 = kProbeTitleUtf8;
      CHECK(ReadTitleViaPresenterOnly(*root) == kProbeTitleUtf8);

      ui::UiLocalizationString::ptr title_copy = pres.title;
      ui::UiString::ptr en_copy = pres.title->strings[0];
      ui::UiDesktopWindowPresenter::ptr presenter_copy =
          root->presented_window->presenter;
      title_copy.Reset();
      en_copy.Reset();
      presenter_copy.Reset();
    }

    root->presented_open = false;
    ui::UnloadPresentedWindow(root->presented_window);
    CHECK(root->presented_window.is_valid());
    CHECK(!root->presented_window.is_loaded());
  }

  {
    DirectoryDomainStorage storage{user_dir};
    ae::Domain domain{storage};
    auto root = LoadApplication<PresenterBranchUnloadRoot>(domain, kRootId);
    CHECK(root->presented_window.is_valid());
    CHECK(!root->presented_window.is_loaded());
    CHECK(root->presented_window.id() == window_id);

    root->presented_window.Load();
    CHECK(root->presented_window->presenter.is_valid());
    CHECK(!root->presented_window->presenter.is_loaded());
    root->presented_window->presenter.Load();
    CHECK(root->presented_window->presenter.id() == presenter_id);

    std::string const presenter_title = ReadTitleViaPresenterOnly(*root);
    std::cout << "UNLOAD_PRESENTED_WINDOW_PRESENTER_BRANCH_DISK_ROUNDTRIP "
              << "presenter_title=" << presenter_title
              << " expected=" << kProbeTitleUtf8
              << " baseline_was=" << kBaselineTitleUtf8 << '\n';
    CHECK(presenter_title == kProbeTitleUtf8);
  }

  std::cout << "UNLOAD_PRESENTED_WINDOW_PRESENTER_BRANCH_DISK_ROUNDTRIP ok "
            << "presenter_id=" << presenter_id.id()
            << " window_id=" << window_id.id() << '\n';
}

}  // namespace
}  // namespace apptraverse::test

int main() {
  apptraverse::EnsureObjectRegistration();
  apptraverse::ui::EnsureUiPresenterRegistration();
  apptraverse::test::EnsurePresenterBranchUnloadRegistration();
  apptraverse::test::RunRoundTrip();
  return 0;
}
