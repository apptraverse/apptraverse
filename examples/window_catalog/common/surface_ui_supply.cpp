#include "surface_ui_session.h"
#include "apptraverse/distill.h"
#include "apptraverse/runtime_node.h"
#include "apptraverse/distilled_domain_export.h"
namespace apptraverse::example::surface_ui {
SurfaceApplication::ptr BuildSurfaceUi(ae::Domain& d, SurfaceFactories factories, bool v1) {
  ae::Domain::ObjectIdSequence ids{d, ae::ObjId{1000}, ae::ObjId{9999}};
  auto app = SurfaceApplication::ptr::Create(ae::CreateWith{d}.with_id(ApplicationId()));
  app->factories = std::move(factories);
  app->v1 = v1;
  app->next_number = 2;
  app->resources.locale = ui::UiLocaleSettings::ptr::Create(ae::CreateWith{d});
  app->resources.locale->BootstrapLanguageIndex(0);
  InitializeRuntimeNode(*app->resources.locale, *app);
  app->resources.device = ui::UiDeviceState::ptr::Create(ae::CreateWith{d});
  app->resources.device->Bootstrap({ui::UiThemeMode::kAuto, false}, {});
  InitializeRuntimeNode(*app->resources.device, *app);
  app->resources.definition = ui::UiLayoutDefinition::ptr::Create(ae::CreateWith{d});
  { auto scope = app->resources.definition->object_scope(); scope.is_constant = true; app->resources.definition->SetObjectScope(scope); }
  auto& root = app->resources.definition->root;
  root.flow = ui::UiLayoutFlow::kAdaptive;
  root.padding = 12;
  root.gap = 8;
  for (auto role : {1001u, 1002u, 1003u}) {
    ui::UiLayoutNode control;
    control.control_id = role;
    control.min_width = role == 1002 ? 150 : 60;
    control.min_height = 32;
    control.alignment = ui::UiLayoutAlignment::kStart;
    root.children.push_back(control);
  }
  app->resources.add_label = MakeSurfaceLabel(d, *app, "Add", "Добавить", 0);
  app->resources.close_label = MakeSurfaceLabel(d, *app, "Close this window", "Закрыть это окно", 0);
  app->resources.language_label = MakeSurfaceLabel(d, *app, "RU", "EN", 0);
  auto initial = MakeSurfaceWindow(*app, 1);
  app->windows = {initial};
  app->current = initial.id();
  app->order = {initial.id()};
  InitializeRuntimeNode(*app);
  return app;
}
std::set<ae::ObjId> SurfaceSupplyConstants(ae::RamDomainStorage const& backing) {
  std::set<ae::ObjId> ids;
  for (auto const& [id, scope] : backing.object_scopes) if (scope.is_constant) ids.insert(id);
  return ids;
}
void MakeSurfaceSupply(ae::RamDomainStorage& backing, SurfaceFactories factories, bool v1) {
  ae::Domain domain{backing};
  auto app = BuildSurfaceUi(domain, std::move(factories), v1);
  FinalizeDistilledGraph(*app);
  SaveDistilledRoot(*app);
}
bool ExportSurfaceSupply(std::filesystem::path const& directory, SurfaceFactories factories, bool v1) {
  ae::RamDomainStorage backing;
  MakeSurfaceSupply(backing, std::move(factories), v1);
  auto constants = SurfaceSupplyConstants(backing);
  return WriteEmbeddedDistilledSupplyHeader(directory / "embedded_surface_ui_supply.h",
      "surface_ui", "APPTRAVERSE_EMBEDDED_SURFACE_UI_SUPPLY_H_", ApplicationId(), {constants.begin(), constants.end()}, backing,
      CollectStoredObjectIds(backing));
}
}
