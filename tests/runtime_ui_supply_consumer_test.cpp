#include <cstdlib>
#include <functional>
#include <iostream>
#include "apptraverse/application_persistence.h"
#include "apptraverse/embedded_application_model_session.h"
#include "apptraverse/platform/windows/win_ui_presenters.h"
#include "apptraverse/platform/windows/win_ui_push_button_presenter.h"
#include "embedded_distilled_runtime_ui_supply.h"
#include "runtime_ui_model.h"
namespace demo = apptraverse::example::runtime_ui;
#define CHECK(x)                                             \
  do {                                                       \
    if (!(x)) {                                              \
      std::cerr << "CHECK " #x " line " << __LINE__ << '\n'; \
      std::exit(1);                                          \
    }                                                        \
  } while (false)
namespace {
demo::RuntimePresenterFactories Factories() {
  return {
      [](ae::Domain& domain) {
        return apptraverse::ui::windows::WinDesktopWindowPresenter::ptr::Create(
            ae::CreateWith{domain});
      },
      [](ae::Domain& domain) {
        return apptraverse::ui::windows::WinPushButtonPresenter::ptr::Create(
            ae::CreateWith{domain});
      }};
}
void Session(ae::RamDomainStorage& backing, ae::RamDomainStorage& user,
             ae::RamDomainStorage& local,
             std::function<void(demo::RuntimeApplication&)> inspect) {
  auto const& supply =
      apptraverse::embedded_distilled::runtime_ui::kEmbeddedGraph;
  apptraverse::ApplicationModelRuntime runtime;
  apptraverse::ApplicationModelSessionHooks<demo::RuntimeApplication> hooks;
  hooks.prepare = [&](auto& app) {
    demo::PrepareRuntimeUi(app, Factories());
    inspect(app);
  };
  hooks.persist = [&](auto& app) {
    apptraverse::CompactApplicationNodesForUserOverlay(app);
    ae::DomainGraph graph{app.domain};
    return graph.SaveRoot(
                    demo::RuntimeApplication::ptr::MakeFromThis(&app).cached(),
                    app.obj_id)
                   .IsErr()
               ? 1
               : 0;
  };
  auto result = apptraverse::RunClientLocalApplicationModelSession(
      user, local, backing, apptraverse::EmbeddedSupplyConstantIdSet(supply),
      demo::RuntimeApplicationId(), runtime,
      [&](auto) {
        CHECK(!runtime.TakePublishedCopyForUiApply().empty());
        runtime.NotifyPublicationConsumed();
        runtime.RequestStop();
      },
      hooks);
  CHECK(result == 0);
}
void CheckSlots(demo::RuntimeApplication& app) {
  auto const& root = app.window->layout_state->EffectiveRoot();
  if (!app.add.is_valid()) {
    CHECK(root.children.empty());
    return;
  }
  CHECK(app.window->layout_state->GetClassId() ==
        demo::RuntimeItemLayoutState::kClassId);
  auto const& repeated = static_cast<demo::RuntimeItemLayoutState const&>(
      *app.window->layout_state);
  CHECK(repeated.application.id() == app.obj_id &&
        &*repeated.application == &app);
  CHECK(root.children.size() == app.items.size() + 1);
  CHECK(root.children[0].control_id == app.add->presenter->control_id);
  for (std::size_t i = 0; i < app.items.size(); ++i) {
    CHECK(root.children[i + 1].control_id ==
          app.items[i]->presenter->control_id);
  }
}
void ChangeTopology(demo::RuntimeApplication& app) {
  if (!app.add.is_valid()) return;
  auto initial = app.items.front().id();
  app.AddItem();
  app.AddItem();
  app.RemoveItem(initial);
  CHECK(app.items.size() == 2 && app.items[0]->number == 2 &&
        app.items[1]->number == 3);
  CHECK(!app.window->layout_state->has_override);
  CheckSlots(app);
}
void ExcludesDefinition(ae::RamDomainStorage const& user,
                        ae::RamDomainStorage const& local,
                        ae::ObjId definition) {
  CHECK(user.state.find(definition) == user.state.end());
  CHECK(local.state.find(definition) == local.state.end());
  for (auto store : {&user, &local}) {
    for (auto const& [id, layers] : store->state) {
      CHECK(!layers ||
            !layers->contains(apptraverse::ui::UiLayoutDefinition::kClassId));
    }
  }
}
}  // namespace
int main() {
  apptraverse::EnsureCoreObjectRegistration();
  apptraverse::ui::windows::EnsureWinControlPresenterRegistration();
  demo::EnsureRuntimeUiRegistration();
  auto const& supply =
      apptraverse::embedded_distilled::runtime_ui::kEmbeddedGraph;
  auto const constants = apptraverse::EmbeddedSupplyConstantIdSet(supply);
  CHECK(constants.size() == 1);
  ae::RamDomainStorage backing_a, backing_b, plain_user, plain_local,
      overridden_user, overridden_local;
  // The independent consumers install the actual header exported by Debug.
  // This test deliberately does not compile or call the graph builder.
  apptraverse::InstallEmbeddedDistilledBacking(backing_a, supply);
  apptraverse::InstallEmbeddedDistilledBacking(backing_b, supply);
  ae::ObjId definition, device;
  double default_padding = -1;
  Session(backing_a, plain_user, plain_local, [&](auto& app) {
    definition = app.window->layout_state->definition.id();
    device = app.window->device_state.id();
    CHECK(constants.contains(definition));
    CHECK(app.window->layout_state->definition->object_scope().is_constant);
    CHECK(!app.window->layout_state->has_override);
    default_padding = app.window->layout_state->EffectiveRoot().padding;
    ChangeTopology(app);
    app.window->device_state->SetAppearance(
        {apptraverse::ui::UiThemeMode::kDark, true});
  });
  ExcludesDefinition(plain_user, plain_local, definition);
  CHECK(plain_user.state.find(device) == plain_user.state.end());
  CHECK(plain_local.state.find(device) != plain_local.state.end());
  Session(backing_a, overridden_user, overridden_local, [&](auto& app) {
    ChangeTopology(app);
    auto layout = app.window->layout_state;
    auto root = layout->EffectiveRoot();
    root.padding = 44;
    root.gap = 19;
    root.flow = apptraverse::ui::UiLayoutFlow::kHorizontal;
    layout->SetOverride(root);
    layout->ReplayFromBase();
    CHECK(layout->has_override && layout->EffectiveRoot().padding == 44);
    CHECK(layout->EffectiveRoot().gap == 19);
    CheckSlots(app);
  });
  ExcludesDefinition(overridden_user, overridden_local, definition);
  // Independently authored compatible supply B keeps identity and changes its
  // default.
  {
    ae::Domain producer{backing_b};
    auto value = apptraverse::ui::UiLayoutDefinition::ptr::Declare(
        ae::CreateWith{producer}.with_id(definition));
    value.Load();
    value->root.padding = default_padding + 17;
    value->root.gap = 23;
    if (!value->root.children.empty()) value->root.children[1].min_width = 155;
    value.Save();
  }
  Session(backing_b, plain_user, plain_local, [&](auto& app) {
    CHECK(!app.window->layout_state->has_override);
    CHECK(app.window->layout_state->EffectiveRoot().padding ==
          default_padding + 17);
    CHECK(app.window->device_state->appearance.soften_dark);
    CheckSlots(app);
    if (app.add.is_valid())
      CHECK(app.window->layout_state->EffectiveRoot().children[1].min_width ==
            155);
  });
  ExcludesDefinition(plain_user, plain_local, definition);
  Session(backing_b, overridden_user, overridden_local, [&](auto& app) {
    auto layout = app.window->layout_state;
    CHECK(layout->has_override && layout->EffectiveRoot().padding == 44);
    CHECK(layout->EffectiveRoot().gap == 19);
    CheckSlots(app);
    layout->ClearOverride();
    CHECK(!layout->has_override &&
          layout->EffectiveRoot().padding == default_padding + 17);
    CHECK(layout->EffectiveRoot().gap == 23);
    CheckSlots(app);
    if (app.add.is_valid())
      CHECK(layout->EffectiveRoot().children[1].min_width == 155);
  });
  ExcludesDefinition(overridden_user, overridden_local, definition);
  Session(backing_b, overridden_user, overridden_local, [&](auto& app) {
    CHECK(!app.window->layout_state->has_override);
    CHECK(app.window->layout_state->EffectiveRoot().padding ==
          default_padding + 17);
    CheckSlots(app);
    if (app.add.is_valid())
      CHECK(app.window->layout_state->EffectiveRoot().children[1].min_width ==
            155);
  });
  ExcludesDefinition(overridden_user, overridden_local, definition);
  std::cout << "PASS exported supply constants: USER/LOCAL exclusion, "
               "independent compatible default, dynamic topology, override "
               "replay/retention, "
               "Clear and cold reload\n";
}
