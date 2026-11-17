#include <cstdlib>
#include <iostream>
#include "apptraverse/application_model_session.h"
#include "apptraverse/embedded_application_model_session.h"
#include "apptraverse/distilled_domain_export.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/model_object_proxy.h"
#include "apptraverse/ui/ui_presented_window_lifecycle.h"
#include "surface_ui_session.h"
namespace demo = apptraverse::example::surface_ui;
#define CHECK(x) do { if (!(x)) { std::cerr << "CHECK " #x " line " << __LINE__ << '\n'; std::exit(1); } } while (false)
demo::SurfaceFactories Factories() {
  return {[](ae::Domain& d) { return apptraverse::ui::UiDesktopWindowPresenter::ptr::Create(ae::CreateWith{d}); },
    [](ae::Domain& d) { return apptraverse::ui::UiPushButtonPresenter::ptr::Create(ae::CreateWith{d}); }};
}
void Session(ae::RamDomainStorage& backing, ae::RamDomainStorage& user, ae::RamDomainStorage& local,
             std::function<void(demo::SurfaceApplication&)> prepare) {
  apptraverse::ApplicationModelRuntime runtime;
  apptraverse::ApplicationModelSessionHooks<demo::SurfaceApplication> hooks;
  hooks.prepare = [&](auto& app) { demo::PrepareSurfaceUi(app, Factories()); prepare(app); };
  hooks.persist = demo::SaveSurfaceUi;
  auto result = apptraverse::RunClientLocalApplicationModelSession(user, local, backing,
      demo::SurfaceSupplyConstants(backing), demo::ApplicationId(), runtime, [&](auto) {
        auto bytes = runtime.TakePublishedCopyForUiApply();
        CHECK(!bytes.empty());
        runtime.NotifyPublicationConsumed();
        runtime.RequestStop();
      }, hooks);
  CHECK(result == 0);
}
int main() {
  apptraverse::EnsureCoreObjectRegistration();
  apptraverse::ui::EnsureUiPresenterRegistration();
  demo::EnsureSurfaceUiRegistration();
  {
    ae::RamDomainStorage scratch;
    ae::Domain domain{scratch};
    auto app = demo::BuildSurfaceUi(domain, Factories(), false);
    apptraverse::PendingDirtyNodes notifications;
    app->SetImmediateEventCompaction(true);
    notifications.Subscribe(*app);
    app->Add();
    auto window = app->windows.back();
    CHECK(window->HasMaterializedChangeNotifier() && window->HasImmediateEventCompaction());
    CHECK(window->layout_state->HasMaterializedChangeNotifier());
    CHECK(window->presenter->title->HasMaterializedChangeNotifier());
    for (auto const& action : window->actions) CHECK(action->HasMaterializedChangeNotifier() && action->HasImmediateEventCompaction());
    std::vector<apptraverse::ModelObjectProxy::ModelWork> queued;
    apptraverse::ModelObjectProxy proxy{[&](auto work) { queued.push_back(std::move(work)); }};
    window->actions[1]->QueueClickFromUi(proxy);
    window->actions[1]->QueueClickFromUi(proxy);
    auto first_id = app->windows.front().id();
    window = {};
    queued[0](domain);
    auto generation = app->Generation();
    queued[1](domain);
    CHECK(app->windows.size() == 1 && app->windows.front().id() == first_id && app->Generation() == generation);
    notifications.ClearAllSubscribedNotifiers(domain);
  }
  ae::RamDomainStorage backing, independent, user, local;
  demo::MakeSurfaceSupply(backing, Factories(), false);
  demo::MakeSurfaceSupply(independent, Factories(), false);
  CHECK(backing.state == independent.state);
  CHECK(backing.object_scopes == independent.object_scopes);
  ae::ObjId retained, removed, definition, device;
  Session(backing, user, local, [&](auto& app) {
    CHECK(app.windows.size() == 1 && app.windows[0]->number == 1);
    definition = app.resources.definition.id(); device = app.resources.device.id();
    auto action = app.windows[0]->actions[0];
    auto action_owner = action->application.id();
    auto action_window = action->window_id;
    auto set_enabled = [&](bool enabled) {
      auto event = apptraverse::ui::SetUiPushButtonEnabledEvent::ptr::Create(ae::CreateWith{*app.domain});
      event->enabled = enabled;
      action->Commit(event);
    };
    set_enabled(false);
    action->ReplayFromBase();
    CHECK(action->application.id() == action_owner && action->role == 1001 && action->window_id == action_window && !action->enabled());
    CHECK(&*action->application == &app);
    set_enabled(true);
    action->CompactJournal(apptraverse::SystemUtcMicros());
    CHECK(action->application.id() == action_owner && action->role == 1001 && action->enabled());
    app.Add(); auto second = app.windows[1]; retained = second.id();
    app.Add(); auto third = app.windows[2]; removed = third.id();
    auto first = app.windows[0];
    app.Remove(removed);
    CHECK(app.windows.size() == 2 && app.windows[0].id() == first.id() && app.windows[1].id() == retained);
    auto generation = app.Generation();
    app.Remove(removed); app.Select(removed);
    CHECK(app.Generation() == generation);
    app.SetOrder({retained.id(), first.id().id(), retained.id()});
    CHECK(app.order.size() == 2 && app.order[0] == retained && app.order[1] == first.id());
    app.Select(retained);
    app.SwitchLanguage();
    CHECK(app.resources.locale->language_index() == 1);
    CHECK(second->presenter->title->Resolve(1) == "Поверхность 2");
    auto edit = apptraverse::ui::SetUiLocalizationSlotUtf8Event::ptr::Create(ae::CreateWith{*app.domain});
    edit->slot_index = 1; edit->utf8 = "Моя кнопка";
    app.resources.add_label->Commit(edit);
    CHECK(app.windows[0]->actions[0]->label->Resolve(1) == "Моя кнопка");
    auto layout = second->layout_state->EffectiveRoot(); layout.padding = 44;
    second->layout_state->SetOverride(layout);
    app.resources.device->SetAppearance({apptraverse::ui::UiThemeMode::kDark, true});
    // Replay adopts the prepared window identities; native factories are not
    // called by Apply, and current selection/order remain ordinary Events.
    auto ids = app.order;
    app.ReplayFromBase();
    CHECK(app.windows.size() == 2 && app.windows[1].id() == retained && app.current == retained && app.order == ids);
  });
  CHECK(user.state.find(device) == user.state.end());
  CHECK(local.state.find(device) != local.state.end());
  CHECK(user.state.find(definition) == user.state.end());
  // A compatible supply revision changes an immutable default at the same ID.
  {
    ae::Domain d{backing};
    auto value = apptraverse::ui::UiLayoutDefinition::ptr::Declare(ae::CreateWith{d}.with_id(definition));
    value.Load(); value->root.padding = 24; value.Save();
  }
  Session(backing, user, local, [&](auto& app) {
    CHECK(app.windows.size() == 2 && app.windows[1].id() == retained);
    CHECK(app.resources.locale->language_index() == 1);
    CHECK(app.resources.add_label->Resolve(1) == "Моя кнопка");
    CHECK(app.resources.device->appearance.soften_dark);
    auto layout = app.windows[1]->layout_state;
    CHECK(layout->EffectiveRoot().padding == 44);
    layout->ClearOverride();
    CHECK(layout->EffectiveRoot().padding == 24);
    CHECK(app.windows[0]->layout_state->EffectiveRoot().padding == 24);
  });
  std::cout << "PASS Surface S0 model: deterministic supply, topology/replay/stale removal, current/order, custom RU label, separate LOCAL and compatible default/override\n";
}
