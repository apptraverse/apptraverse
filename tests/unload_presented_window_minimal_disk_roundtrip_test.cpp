#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

#include "aether-objects/obj/domain.h"

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/distill.h"
#include "apptraverse/ui/ui_presenters.h"

#include "minimal_app_controls.h"
#include "minimal_app_graph_headless.h"
#include "minimal_app_persistence.h"
#include "minimal_app_prepare.h"
#include "minimal_app_window_lifecycle.h"
#include "minimal_application.h"
#include "apptraverse/ui/ui_presented_window_lifecycle.h"

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

constexpr char kBaselineTitleUtf8[] = "Detail Panel";
constexpr char kProbeTitleUtf8[] = "UnloadRoundtripPresenterLeaf-X7";

std::filesystem::path UserDir() {
  char const* suffix = std::getenv("APPTRAVERSE_UNLOAD_RT_USER_SUFFIX");
  auto dir = std::filesystem::temp_directory_path() /
             "unload_presented_window_minimal_disk_roundtrip";
  if (suffix != nullptr && suffix[0] != '\0') {
    dir += suffix;
  }
  return dir;
}

std::string ResolveAuxTitleViaCloser(example::minimal::MinimalApplication& app) {
  app.auxiliary.closer.Load();
  CHECK(app.auxiliary.closer.is_valid());
  app.auxiliary.closer->window_title.Load();
  CHECK(app.auxiliary.closer->window_title.is_valid());
  app.auxiliary.closer->window_title->strings[0].Load();
  CHECK(app.auxiliary.closer->window_title->strings[0].is_valid());
  return app.auxiliary.closer->window_title->strings[0]->utf8;
}

std::string ResolveAuxTitleViaPresenter(example::minimal::MinimalApplication& app) {
  app.auxiliary.window.Load();
  app.auxiliary.window->presenter.Load();
  auto& pres = static_cast<ui::UiDesktopWindowPresenter&>(
      *app.auxiliary.window->presenter);
  pres.title.Load();
  CHECK(pres.title.is_valid());
  pres.title->strings[0].Load();
  CHECK(pres.title->strings[0].is_valid());
  return pres.title->strings[0]->utf8;
}

std::string ReadTitleFromUserProfile(DirectoryDomainStorage& storage,
                                     bool open_auxiliary_for_presenter_path) {
  ae::Domain domain{storage};
  auto app = LoadApplication<example::minimal::MinimalApplication>(
      domain, example::minimal::MinimalApplicationRootId());
  if (open_auxiliary_for_presenter_path) {
    example::minimal::PrepareMinimalApplicationForRun(*app);
    app->OpenAuxiliaryWindowImpl();
    return ResolveAuxTitleViaPresenter(*app);
  }
  return ResolveAuxTitleViaCloser(*app);
}

void RunRoundTrip() {
  std::filesystem::path const user_dir = UserDir();
  std::error_code ec;
  std::filesystem::remove_all(user_dir, ec);

  example::minimal::BootstrapMinimalApplicationProfileIfAbsent(
      user_dir, [](ae::Domain& domain) {
        auto app = example::minimal::BuildHeadlessMinimalApplicationGraph(domain);
        example::minimal::PrepareMinimalApplicationForRun(*app);
        return app;
      });

  {
    DirectoryDomainStorage storage{user_dir};
    CHECK(ReadTitleFromUserProfile(storage, false) == kBaselineTitleUtf8);
  }

  ae::ObjId presenter_id{};
  ae::ObjId window_id{};

  {
    DirectoryDomainStorage storage{user_dir};
    InstrumentedDomainStorage instrumented{storage, user_dir, &storage};
    ae::Domain domain{instrumented};
    auto app = LoadApplication<example::minimal::MinimalApplication>(
        domain, example::minimal::MinimalApplicationRootId());
    example::minimal::PrepareMinimalApplicationForRun(*app);
    app->OpenAuxiliaryWindowImpl();
    CHECK(app->auxiliary_presented_open);
    CHECK(ResolveAuxTitleViaPresenter(*app) == kBaselineTitleUtf8);

    app->auxiliary.window.Load();
    app->auxiliary.window->presenter.Load();
    presenter_id = app->auxiliary.window->presenter.id();
    window_id = app->auxiliary.window.id();
    CHECK(presenter_id.is_valid());

    auto& pres = static_cast<ui::UiDesktopWindowPresenter&>(
        *app->auxiliary.window->presenter);
    pres.title.Load();
    pres.title->strings[0].Load();
    pres.title->strings[0]->utf8 = kProbeTitleUtf8;
    CHECK(ResolveAuxTitleViaPresenter(*app) == kProbeTitleUtf8);
    CHECK(ResolveAuxTitleViaCloser(*app) == kProbeTitleUtf8);
    CHECK(instrumented.SnapshotStats().store_calls == 0);

    app->CloseAuxiliaryWindowImpl();
    CHECK(instrumented.SnapshotStats().store_calls > 0);
    CHECK(!app->auxiliary_presented_open);
    CHECK(app->auxiliary.window.is_valid());
    CHECK(!app->auxiliary.window.is_loaded());
  }

  std::string const disk_title_after_close = [&] {
    DirectoryDomainStorage storage{user_dir};
    return ReadTitleFromUserProfile(storage, false);
  }();

  {
    DirectoryDomainStorage storage{user_dir};
    ae::Domain domain{storage};
    auto app = LoadApplication<example::minimal::MinimalApplication>(
        domain, example::minimal::MinimalApplicationRootId());
    CHECK(!app->auxiliary_presented_open);
    CHECK(app->auxiliary.window.is_valid());
    CHECK(!app->auxiliary.window.is_loaded());
    CHECK(app->auxiliary.window.id() == window_id);

    std::string const closer_title = ResolveAuxTitleViaCloser(*app);
    std::cout << "UNLOAD_PRESENTED_WINDOW_MINIMAL_DISK_ROUNDTRIP "
              << "disk_title=" << disk_title_after_close
              << " closer_title=" << closer_title << '\n';

    app->auxiliary.window.Load();
    CHECK(app->auxiliary.window->presenter.is_valid());
    CHECK(!app->auxiliary.window->presenter.is_loaded());
    app->auxiliary.window->presenter.Load();
    CHECK(app->auxiliary.window->presenter.id() == presenter_id);

    std::string const presenter_title = ResolveAuxTitleViaPresenter(*app);
    CHECK(presenter_title == kProbeTitleUtf8);
    CHECK(closer_title == kProbeTitleUtf8);
    CHECK(disk_title_after_close == kProbeTitleUtf8);
  }

  std::cout << "UNLOAD_PRESENTED_WINDOW_MINIMAL_DISK_ROUNDTRIP ok presenter_id="
            << presenter_id.id() << " window_id=" << window_id.id()
            << " expected=" << kProbeTitleUtf8
            << " baseline_was=" << kBaselineTitleUtf8 << '\n';
}

}  // namespace
}  // namespace apptraverse::test

int main() {
  apptraverse::example::minimal::EnsureHeadlessMinimalApplicationGraphRegistration();
  apptraverse::test::RunRoundTrip();
  return 0;
}
