#if defined(_WIN32)

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "aether-objects/obj/domain.h"
#include "aether-objects/obj/registry.h"

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/distill.h"
#include "apptraverse/object_serialization.h"
#include "apptraverse/runtime_node.h"

#include "apptraverse/platform/windows/surfaces_win_presenters.h"
#include "apptraverse/platform/windows/win_ui_push_button_presenter.h"
#include "apptraverse/platform/windows/win_ui_presenters.h"
#include "apptraverse/surfaces/surfaces_chrome_wiring.h"
#include "apptraverse/surfaces/surfaces_ids.h"
#include "apptraverse/surfaces/surfaces_localization.h"
#include "apptraverse/surfaces/surfaces_model.h"
#include "apptraverse/surfaces/surfaces_registration.h"
#include "apptraverse/ui/ui_locale_settings.h"
#include "apptraverse/ui/ui_push_button.h"
#include "apptraverse/ui/ui_string.h"

#include "surfaces_demo_language_button.h"
#include "surfaces_demo_layout_push_button_presenter.h"
#include "surfaces_demo_surface_window_host.h"
#include "surfaces_demo_win32_registration.h"
#include "surfaces_distill.h"

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
  EnsureWin32SurfacesPresenterRegistration();
  example::surfaces::EnsureSurfacesDemoWin32Registration();
}

std::filesystem::path TempDir(char const* name) {
  auto path = std::filesystem::temp_directory_path() / name;
  std::filesystem::remove_all(path);
  return path;
}

ui::UiLocalizationString::ptr MakeTwoSlotLabel(ae::Domain& domain,
                                               Surfaces& surfaces,
                                               char const* slot0,
                                               char const* slot1) {
  auto localized = ui::UiLocalizationString::ptr::Create(
      ae::CreateWith{domain});
  InitializeRuntimeNode(*localized, surfaces);
  auto s0 = ui::UiString::ptr::Create(ae::CreateWith{domain});
  s0->utf8 = slot0;
  auto s1 = ui::UiString::ptr::Create(ae::CreateWith{domain});
  s1->utf8 = slot1;
  localized->strings = {s0, s1};
  return localized;
}

std::vector<std::uint32_t> ListStoredObjIds(std::filesystem::path const& root) {
  std::vector<std::uint32_t> ids;
  if (!std::filesystem::exists(root)) {
    return ids;
  }
  for (auto const& entry : std::filesystem::directory_iterator(root)) {
    if (!entry.is_directory()) {
      continue;
    }
    try {
      ids.push_back(
          static_cast<std::uint32_t>(std::stoul(entry.path().filename().string())));
    } catch (...) {
    }
  }
  std::sort(ids.begin(), ids.end());
  return ids;
}

bool TryLoadObjRoot(ae::Domain& domain, ae::ObjId id, char const* path_label,
                    DirectoryDomainStorage& storage) {
  ae::ClassList layers = storage.Enumerate(id);
  auto& registry = ae::Registry::GetRegistry();
  std::cout << "  path=" << path_label << " obj_id=" << id.id()
            << " stored_layers:";
  for (std::uint32_t c : layers) {
    std::cout << ' ' << c;
    if (!registry.IsExisting(c)) {
      std::cout << "(unregistered)";
    }
  }
  std::cout << '\n';

  ae::DomainGraph graph{&domain};
  ae::Ptr<ae::Obj> ptr;
  auto load_result = graph.LoadRoot(id, ptr);
  if (load_result.IsErr() || !ptr) {
    std::cout << "  FAIL DomainGraph::LoadRoot (materialize/hydrate or nested ref)\n";
    return false;
  }
  std::cout << "  OK runtime_class_id=" << ptr->GetClassId() << '\n';
  return true;
}

void DiagnoseStoredGraphLoads(ae::Domain& domain,
                              DirectoryDomainStorage& storage,
                              std::filesystem::path const& profile_root,
                              ae::ObjId application_id) {
  std::cout << "Load diagnostics (per stored object, layer-by-layer):\n";
  (void)TryLoadObjRoot(domain, application_id, "Application", storage);

  for (std::uint32_t obj_id : ListStoredObjIds(profile_root)) {
    if (obj_id == application_id.id()) {
      continue;
    }
    char label[64];
    std::snprintf(label, sizeof(label), "obj/%u", obj_id);
    TryLoadObjRoot(domain, ae::ObjId{obj_id}, label, storage);
  }
}

Application::ptr TryLoadApplicationNoAbort(ae::Domain& domain, ae::ObjId id) {
  auto root = Application::ptr::Declare(ae::CreateWith{domain}.with_id(id));
  root.Load();
  return root;
}

void AssertDemoRegistryHasNoLayoutHeir() {
  using Layout = example::surfaces::SurfacesDemoLayoutPushButtonPresenter;
  CHECK(!ae::Registry::GetRegistry().IsExisting(Layout::kClassId));
  std::cout << "Demo registry: Layout ClassId " << Layout::kClassId
            << " not registered (expected)\n";
}

void ProbeA_WinPushButtonStaysWin() {
  std::cout << "\n=== Probe A: WinPushButtonPresenter save/load ===\n";
  EnsureRegistration();
  AssertDemoRegistryHasNoLayoutHeir();

  auto const dir = TempDir("apptraverse_probe_a_win_push_button");
  DirectoryDomainStorage storage{dir};
  ae::Domain domain{storage};

  auto locale = ui::UiLocaleSettings::ptr::Create(ae::CreateWith{domain});
  auto host =
      example::surfaces::SurfacesDemoSurfaceWindowHostPresenter::ptr::Create(
          ae::CreateWith{domain});
  host->locale = locale;
  auto button = ui::UiPushButton::ptr::Create(ae::CreateWith{domain});
  auto win = ui::windows::WinPushButtonPresenter::ptr::Create(
      ae::CreateWith{domain});
  win->button = button;
  win->window_presenter = host;
  win->layout_x = 4;
  win->layout_y = 5;
  win->layout_width = 90;
  win->layout_height = 22;
  win->control_id = 1001;

  std::uint32_t const id = win->obj_id.id();
  std::uint32_t const class_before = win->GetClassId();
  ui::windows::WinPushButtonPresenter::ptr::MakeFromThis(&*win).Save();

  DirectoryDomainStorage load_storage{dir};
  ae::Domain load_domain{load_storage};
  auto loaded = ui::windows::WinPushButtonPresenter::ptr::Declare(
      ae::CreateWith{load_domain}.with_id(ae::ObjId{id}));
  loaded.Load();
  CHECK(loaded);
  std::uint32_t const class_after = loaded->GetClassId();

  std::cout << "ClassId before save: " << class_before << '\n';
  std::cout << "ClassId after load:  " << class_after << '\n';
  CHECK(class_before == ui::windows::WinPushButtonPresenter::kClassId);
  CHECK(class_after == ui::windows::WinPushButtonPresenter::kClassId);
}

Application::ptr BuildMinimalLanguageGraph(ae::Domain& domain,
                                           char const* switch_slot0,
                                           char const* switch_slot1) {
  using surfaces_demo::ObjId;
  using surfaces_demo::ToObjId;
  auto application = Application::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(ObjId::Application)));
  auto surfaces = Surfaces::ptr::Create(ae::CreateWith{domain});
  auto surface1 = Surface::ptr::Create(ae::CreateWith{domain});
  surface1->number = 1;
  AssignInitialDesktopBounds(*surface1);
  surface1->surfaces = surfaces;
  surfaces->surfaces.push_back(surface1);
  application->surfaces = surfaces;

  InstallSurfacesDemoLocalization(*application, domain);
  application->language_switch_label =
      MakeTwoSlotLabel(domain, *surfaces, switch_slot0, switch_slot1);

  Surface& surface = *surface1;
  auto host =
      example::surfaces::SurfacesDemoSurfaceWindowHostPresenter::ptr::Create(
          ae::CreateWith{domain});
  auto language_button =
      example::surfaces::SurfacesDemoLanguageButton::ptr::Create(
          ae::CreateWith{domain});
  auto language_presenter = ui::windows::WinPushButtonPresenter::ptr::Create(
      ae::CreateWith{domain});

  language_button->locale = application->locale;
  language_button->label = application->language_switch_label;
  host->locale = application->locale;

  language_presenter->button = language_button;
  language_presenter->window_presenter = host;
  language_presenter->control_id = 1003;
  language_presenter->layout_x = 10;
  language_presenter->layout_y = 11;
  language_presenter->layout_width = 120;
  language_presenter->layout_height = 24;

  surface.chrome.language_button = language_presenter;
  WireSurfaceChromeLocalization(surface, *application);
  return application;
}

void VerifyLanguageOnly(Surface& surface, Application& application,
                        char const* tag, char const* expect_slot0,
                        char const* expect_slot1) {
  auto lang = ui::windows::WinPushButtonPresenter::ptr{
      surface.chrome.language_button};
  CHECK(lang);
  CHECK(lang->GetClassId() == ui::windows::WinPushButtonPresenter::kClassId);
  CHECK(lang->window_presenter && lang->window_presenter->locale);
  CHECK(&*lang->window_presenter->locale == &*application.locale);

  auto lang_button =
      example::surfaces::SurfacesDemoLanguageButton::ptr{lang->button};
  CHECK(&*lang_button->label == &*application.language_switch_label);
  CHECK(&*lang_button->locale == &*application.locale);
  CHECK(application.language_switch_label->Resolve(0) == expect_slot0);
  CHECK(application.language_switch_label->Resolve(1) == expect_slot1);
  CHECK(lang_button->label->Resolve(0) == expect_slot0);
  CHECK(lang_button->label->Resolve(1) == expect_slot1);

  std::cout << tag << ": lang_id=" << lang->obj_id.id() << " class="
            << lang->GetClassId() << " label0=" << expect_slot0
            << " label1=" << expect_slot1 << '\n';
}

void VerifyDemoChrome(Surface& surface, Application& application,
                      char const* tag, char const* expect_slot0,
                      char const* expect_slot1) {
  auto add = ui::windows::WinPushButtonPresenter::ptr{surface.chrome.add_button};
  auto lang = ui::windows::WinPushButtonPresenter::ptr{
      surface.chrome.language_button};
  auto close = ui::windows::WinPushButtonPresenter::ptr{
      surface.chrome.close_button};
  CHECK(add && lang && close);
  CHECK(add->obj_id != lang->obj_id);
  CHECK(add->obj_id != close->obj_id);
  CHECK(lang->obj_id != close->obj_id);
  CHECK(add->GetClassId() == ui::windows::WinPushButtonPresenter::kClassId);
  CHECK(close->GetClassId() == ui::windows::WinPushButtonPresenter::kClassId);
  CHECK(&*add->window_presenter == &*lang->window_presenter);
  CHECK(&*close->window_presenter == &*add->window_presenter);
  VerifyLanguageOnly(surface, application, tag, expect_slot0, expect_slot1);
  std::cout << tag << ": add_id=" << add->obj_id.id()
            << " lang_id=" << lang->obj_id.id()
            << " close_id=" << close->obj_id.id() << " shared_host=yes\n";
}

void ProbeB_MinimalLanguageGraph() {
  std::cout << "\n=== Probe B: minimal Language graph ===\n";
  EnsureRegistration();
  AssertDemoRegistryHasNoLayoutHeir();

  auto const dir = TempDir("apptraverse_probe_b_language_minimal");
  DirectoryDomainStorage storage{dir};
  ae::Domain domain{storage};
  auto application =
      BuildMinimalLanguageGraph(domain, "Switch-Alpha", "Switch-Beta");
  FinalizeDistilledGraph(*application);
  Surface& surface = *application->surfaces->surfaces[0];
  CHECK(surface.chrome.language_button);
  auto lang_presenter = ui::windows::WinPushButtonPresenter::ptr{
      surface.chrome.language_button};
  CHECK(lang_presenter->button->label->Resolve(0) == "Switch-Alpha");
  CHECK(lang_presenter->button->label->Resolve(1) == "Switch-Beta");

  SaveDistilledRoot(*application);

  DirectoryDomainStorage load_storage{dir};
  ae::Domain load_domain{load_storage};
  ae::ObjId app_id{surfaces_demo::ToObjId(surfaces_demo::ObjId::Application)};
  auto loaded = TryLoadApplicationNoAbort(load_domain, app_id);
  if (!loaded) {
    DiagnoseStoredGraphLoads(load_domain, load_storage, dir, app_id);
    CHECK(loaded);
  }
  VerifyLanguageOnly(*loaded->surfaces->surfaces[0], *loaded,
                     "After save/load (minimal)", "Switch-Alpha",
                     "Switch-Beta");

  ByteSink sink;
  SerializeInitialPublication(*loaded, sink);
  ae::RamDomainStorage ui_storage;
  ae::Domain ui_domain{ui_storage};
  ByteSource in;
  in.data = sink.bytes.data();
  in.size = sink.bytes.size();
  auto ui_root = LoadInitialPublication(in, ui_domain, ui_storage);
  auto ui_app = Application::ptr::MakeFromThis(
      static_cast<Application*>(ui_root.get()));
  VerifyLanguageOnly(*ui_app->surfaces->surfaces[0], *ui_app,
                     "After initial publication (UI domain)",
                     "Switch-Alpha", "Switch-Beta");
}

void ProbeC_RealBuildSurfacesGraph() {
  std::cout << "\n=== Probe C: BuildSurfacesGraph save/load ===\n";
  EnsureRegistration();
  AssertDemoRegistryHasNoLayoutHeir();

  auto const demo_dir = TempDir("apptraverse_probe_c_surfaces_demo_graph");
  DirectoryDomainStorage demo_storage{demo_dir};
  ae::Domain demo_domain{demo_storage};
  auto demo_app = BuildSurfacesGraph(demo_domain);
  demo_app->language_switch_label = MakeTwoSlotLabel(
      demo_domain, *demo_app->surfaces, "Demo-X", "Demo-Y");
  WireSurfaceChromeLocalization(*demo_app->surfaces->surfaces[0], *demo_app);
  FinalizeDistilledGraph(*demo_app);
  Surface& demo_surface = *demo_app->surfaces->surfaces[0];
  CHECK(demo_surface.chrome.add_button);
  CHECK(demo_surface.chrome.language_button);

  SaveDistilledRoot(*demo_app);

  for (std::uint32_t obj_id : ListStoredObjIds(demo_dir)) {
    ae::ClassList layers = demo_storage.Enumerate(ae::ObjId{obj_id});
    for (std::uint32_t c : layers) {
      CHECK(c != example::surfaces::SurfacesDemoLayoutPushButtonPresenter::
                    kClassId);
    }
  }

  DirectoryDomainStorage demo_load_storage{demo_dir};
  ae::Domain demo_load_domain{demo_load_storage};
  ae::ObjId app_id{surfaces_demo::ToObjId(surfaces_demo::ObjId::Application)};
  auto demo_loaded = TryLoadApplicationNoAbort(demo_load_domain, app_id);
  if (!demo_loaded) {
    DiagnoseStoredGraphLoads(demo_load_domain, demo_load_storage, demo_dir,
                             app_id);
    CHECK(demo_loaded);
  }
  VerifyDemoChrome(*demo_loaded->surfaces->surfaces[0], *demo_loaded,
                   "Demo after save/load", "Demo-X", "Demo-Y");

  ByteSink sink;
  SerializeInitialPublication(*demo_loaded, sink);
  ae::RamDomainStorage ui_storage;
  ae::Domain ui_domain{ui_storage};
  ByteSource in;
  in.data = sink.bytes.data();
  in.size = sink.bytes.size();
  auto ui_root = LoadInitialPublication(in, ui_domain, ui_storage);
  auto ui_app = Application::ptr::MakeFromThis(
      static_cast<Application*>(ui_root.get()));
  VerifyDemoChrome(*ui_app->surfaces->surfaces[0], *ui_app,
                   "Demo after initial publication (UI domain)", "Demo-X",
                   "Demo-Y");
}

}  // namespace
}  // namespace apptraverse::test

int main() {
  apptraverse::test::ProbeA_WinPushButtonStaysWin();
  apptraverse::test::ProbeB_MinimalLanguageGraph();
  apptraverse::test::ProbeC_RealBuildSurfacesGraph();
  std::cout << "\nsurfaces_language_isolated_probe_test OK\n";
  return 0;
}

#endif
