#if defined(_WIN32)

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

#include "aether-objects/obj/domain.h"
#include "aether-objects/obj/registry.h"

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/object_serialization.h"
#include "apptraverse/runtime_node.h"

#include "apptraverse/platform/windows/win_ui_push_button_presenter.h"
#include "apptraverse/platform/windows/win_ui_presenters.h"
#include "apptraverse/surfaces/surfaces_registration.h"
#include "apptraverse/ui/ui_locale_settings.h"
#include "apptraverse/ui/ui_push_button.h"

#include "surfaces_demo_language_button.h"
#include "surfaces_demo_layout_push_button_presenter.h"
#include "surfaces_demo_surface_window_host.h"
#include "surfaces_demo_win32_layout_repro_registration.h"
#include "surfaces_demo_win32_registration.h"

namespace apptraverse::test {
namespace {

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"       \
                << __LINE__ << '\n';                                         \
      std::exit(2);                                                          \
    }                                                                        \
  } while (0)

void EnsureRegistration() {
  EnsureObjectRegistration();
  EnsureSurfacesRegistration();
  ui::EnsureUiPresenterRegistration();
  ui::windows::EnsureWinUiPresenterRegistration();
  example::surfaces::EnsureSurfacesDemoWin32Registration();
  example::surfaces::EnsureSurfacesDemoWin32LayoutReproRegistration();
}

std::filesystem::path TempDir(char const* name) {
  auto path = std::filesystem::temp_directory_path() / name;
  std::filesystem::remove_all(path);
  return path;
}

void PrintLayerSummary(char const* label, ae::ObjId id,
                       DirectoryDomainStorage& storage,
                       std::uint32_t runtime_class_id) {
  ae::ClassList layers = storage.Enumerate(id);
  std::cout << label << " obj_id=" << id.id()
            << " runtime_class_id=" << runtime_class_id << '\n';
  std::cout << "  stored_class_layers:";
  for (std::uint32_t c : layers) {
    std::cout << ' ' << c;
  }
  std::cout << '\n';
}

void WireMinimalPresenter(ui::windows::WinPushButtonPresenter& presenter,
                          ae::Domain& domain,
                          ui::UiPushButton::ptr button,
                          example::surfaces::SurfacesDemoSurfaceWindowHostPresenter::
                              ptr host) {
  presenter.button = button;
  presenter.window_presenter = host;
  presenter.layout_x = 1;
  presenter.layout_y = 2;
  presenter.layout_width = 80;
  presenter.layout_height = 24;
  presenter.control_id = 1001;
}

void ProbeA_FactorySelection() {
  std::cout << "\n=== Probe A: factory selection (isolated presenters) ===\n";
  EnsureRegistration();

  CHECK(ae::Registry::GetRegistry().IsExisting(
      example::surfaces::SurfacesDemoLayoutPushButtonPresenter::kClassId));

  auto const dir = TempDir("apptraverse_probe_a_push_button_factory");
  DirectoryDomainStorage storage{dir};
  ae::Domain domain{storage};

  auto locale = ui::UiLocaleSettings::ptr::Create(ae::CreateWith{domain});
  auto host =
      example::surfaces::SurfacesDemoSurfaceWindowHostPresenter::ptr::Create(
          ae::CreateWith{domain});
  host->locale = locale;

  auto win_button = ui::UiPushButton::ptr::Create(ae::CreateWith{domain});
  auto layout_button =
      example::surfaces::SurfacesDemoLanguageButton::ptr::Create(
          ae::CreateWith{domain});

  auto win = ui::windows::WinPushButtonPresenter::ptr::Create(
      ae::CreateWith{domain});
  auto layout =
      example::surfaces::SurfacesDemoLayoutPushButtonPresenter::ptr::Create(
          ae::CreateWith{domain});

  WireMinimalPresenter(*win, domain, win_button, host);
  WireMinimalPresenter(*layout, domain, layout_button, host);
  layout->control_id = 1003;

  std::uint32_t const win_id = win->obj_id.id();
  std::uint32_t const layout_id = layout->obj_id.id();
  std::uint32_t const win_class_before = win->GetClassId();
  std::uint32_t const layout_class_before = layout->GetClassId();

  ui::windows::WinPushButtonPresenter::ptr::MakeFromThis(&*win).Save();
  example::surfaces::SurfacesDemoLayoutPushButtonPresenter::ptr::MakeFromThis(
      &*layout)
      .Save();

  DirectoryDomainStorage load_storage{dir};
  ae::Domain load_domain{load_storage};

  auto loaded_win = ui::windows::WinPushButtonPresenter::ptr::Declare(
      ae::CreateWith{load_domain}.with_id(ae::ObjId{win_id}));
  loaded_win.Load();
  auto loaded_layout =
      example::surfaces::SurfacesDemoLayoutPushButtonPresenter::ptr::Declare(
          ae::CreateWith{load_domain}.with_id(ae::ObjId{layout_id}));
  loaded_layout.Load();

  PrintLayerSummary("Win before save", ae::ObjId{win_id}, storage,
                    win_class_before);
  PrintLayerSummary("Win after load", ae::ObjId{win_id}, load_storage,
                    loaded_win->GetClassId());
  PrintLayerSummary("Layout before save", ae::ObjId{layout_id}, storage,
                    layout_class_before);
  PrintLayerSummary("Layout after load", ae::ObjId{layout_id}, load_storage,
                    loaded_layout->GetClassId());

  bool const win_becomes_layout =
      loaded_win->GetClassId() !=
      ui::windows::WinPushButtonPresenter::kClassId;
  std::cout << "Win stored ClassId (before save): " << win_class_before << '\n';
  std::cout << "Win loaded ClassId: " << loaded_win->GetClassId() << '\n';
  std::cout << "Win object became Layout after load: "
            << (win_becomes_layout ? "YES (repro)" : "NO") << '\n';
}

}  // namespace
}  // namespace apptraverse::test

int main() {
  apptraverse::test::ProbeA_FactorySelection();
  std::cout << "\nsurfaces_factory_layout_leaf_repro_test OK\n";
  return 0;
}

#endif
