#include "runtime_ui_supply.h"
#include "apptraverse/distill.h"
#include "apptraverse/runtime_node.h"
#include "apptraverse/ui/ui_presented_window_composition.h"
namespace apptraverse::example::runtime_ui {
RuntimeApplication::ptr BuildRuntimeUiGraph(
    ae::Domain& domain, bool dynamic, RuntimePresenterFactories factories) {
  auto app = RuntimeApplication::ptr::Create(
      ae::CreateWith{domain}.with_id(RuntimeApplicationId()));
  app->factories = std::move(factories);
  app->window = ui::UiDesktopWindow::ptr::Create(ae::CreateWith{domain});
  app->window->BootstrapFrame(120, 100, 640, 480);
  auto locale = ui::UiLocaleSettings::ptr::Create(ae::CreateWith{domain});
  locale->BootstrapLanguageIndex(0);
  InitializeRuntimeNode(*locale, *app);
  auto device = ui::UiDeviceState::ptr::Create(ae::CreateWith{domain});
  device->Bootstrap({ui::UiThemeMode::kAuto, false}, {});
  InitializeRuntimeNode(*device, *app);
  auto definition = ui::UiLayoutDefinition::ptr::Create(ae::CreateWith{domain});
  auto scope = definition->object_scope();
  scope.is_constant = true;
  definition->SetObjectScope(scope);
  definition->root.flow = ui::UiLayoutFlow::kVertical;
  ui::UiLayoutState::ptr layout;
  if (dynamic) {
    auto repeated = RuntimeItemLayoutState::ptr::Create(ae::CreateWith{domain});
    repeated->application = app;
    layout = repeated;
  } else {
    layout = ui::UiLayoutState::ptr::Create(ae::CreateWith{domain});
  }
  layout->Bootstrap(definition, false, {});
  InitializeRuntimeNode(*layout, *app);
  auto title = MakeRuntimeUiLabel(
      domain, *app, dynamic ? "Dynamic Objects" : "Main Window Runtime");
  ui::ComposePresentedWindow(app->window, app->factories.window(domain), title,
                             locale, layout, device);
  InitializeRuntimeNode(*app->window, *app);
  if (dynamic) {
    app->add = MakeRuntimeUiButton(*app, 0);
    app->items.push_back(MakeRuntimeUiButton(*app, 1));
    definition->root.padding = 12;
    definition->root.gap = 8;
    for (auto id : {100u, 1001u}) {
      ui::UiLayoutNode slot;
      slot.control_id = id;
      slot.min_width = 120;
      slot.min_height = 28;
      definition->root.children.push_back(slot);
    }
  }
  InitializeRuntimeNode(*app);
  return app;
}

std::set<ae::ObjId> RuntimeUiSupplyConstants(
    ae::RamDomainStorage const& backing) {
  std::set<ae::ObjId> constants;
  for (auto const& [id, scope] : backing.object_scopes) {
    if (scope.is_constant) constants.insert(id);
  }
  return constants;
}
void MakeRuntimeUiSupply(ae::RamDomainStorage& backing, bool dynamic,
                         RuntimePresenterFactories factories) {
  ae::Domain domain{backing};
  ae::Domain::ObjectIdSequence ids{domain, ae::ObjId{10000}, ae::ObjId{20000}};
  auto app = BuildRuntimeUiGraph(domain, dynamic, std::move(factories));
  FinalizeDistilledGraph(*app);
  SaveDistilledRoot(*app);
}
}  // namespace apptraverse::example::runtime_ui
