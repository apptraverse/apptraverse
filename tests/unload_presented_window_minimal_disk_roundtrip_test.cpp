#include <cstdlib>
#include <filesystem>
#include <iostream>

#include "aether-objects/obj/domain.h"

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/distill.h"
#include "apptraverse/ui/ui_desktop_window.h"
#include "apptraverse/ui/ui_presenters.h"

#include "minimal_app_controls.h"
#include "minimal_app_graph_headless.h"
#include "minimal_app_persistence.h"
#include "minimal_app_prepare.h"
#include "minimal_app_window_lifecycle.h"
#include "minimal_application.h"

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

constexpr char kProbeTitleUtf8[] = "UnloadRoundtripPresenterLeaf-X7";

std::filesystem::path UserDir() {
  return std::filesystem::temp_directory_path() /
         "unload_presented_window_minimal_disk_roundtrip";
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

  ae::ObjId presenter_id{};
  ae::ObjId window_id{};

  {
    DirectoryDomainStorage storage{user_dir};
    ae::Domain domain{storage};
    auto app = LoadApplication<example::minimal::MinimalApplication>(
        domain, example::minimal::MinimalApplicationRootId());
    example::minimal::PrepareMinimalApplicationForRun(*app);
    app->OpenAuxiliaryWindowImpl();
    CHECK(app->auxiliary_presented_open);
    app->auxiliary.window.Load();
    app->auxiliary.window->presenter.Load();
    presenter_id = app->auxiliary.window->presenter.id();
    window_id = app->auxiliary.window.id();
    CHECK(presenter_id.is_valid());
    auto& pres = static_cast<ui::UiDesktopWindowPresenter&>(
        *app->auxiliary.window->presenter);
    pres.title.Load();
    CHECK(pres.title.is_valid());
    pres.title->strings[0].Load();
    CHECK(pres.title->strings[0].is_valid());
    pres.title->strings[0]->utf8 = kProbeTitleUtf8;

    example::minimal::SealAuxiliaryWindowClosed(*app);
    CHECK(!app->auxiliary_presented_open);
    CHECK(app->auxiliary.window.is_valid());
    CHECK(!app->auxiliary.window.is_loaded());
  }

  {
    DirectoryDomainStorage storage{user_dir};
    ae::Domain domain{storage};
    auto app = LoadApplication<example::minimal::MinimalApplication>(
        domain, example::minimal::MinimalApplicationRootId());
    CHECK(!app->auxiliary_presented_open);
    CHECK(app->auxiliary.window.is_valid());
    CHECK(!app->auxiliary.window.is_loaded());
    CHECK(app->auxiliary.window.id() == window_id);

    app->auxiliary.window.Load();
    CHECK(app->auxiliary.window->presenter.is_valid());
    CHECK(!app->auxiliary.window->presenter.is_loaded());
    app->auxiliary.window->presenter.Load();
    CHECK(app->auxiliary.window->presenter.id() == presenter_id);

    auto& pres = static_cast<ui::UiDesktopWindowPresenter&>(
        *app->auxiliary.window->presenter);
    pres.title.Load();
    CHECK(pres.title.is_valid());
    pres.title->strings[0].Load();
    CHECK(pres.title->strings[0]->utf8 == kProbeTitleUtf8);
    CHECK(pres.title->Resolve(0) == kProbeTitleUtf8);
  }

  std::cout << "UNLOAD_PRESENTED_WINDOW_MINIMAL_DISK_ROUNDTRIP ok presenter_id="
            << presenter_id.id() << " window_id=" << window_id.id()
            << " title=" << kProbeTitleUtf8 << '\n';
}

}  // namespace
}  // namespace apptraverse::test

int main() {
  apptraverse::EnsureObjectRegistration();
  apptraverse::example::minimal::EnsureHeadlessMinimalApplicationGraphRegistration();
  apptraverse::test::RunRoundTrip();
  return 0;
}
