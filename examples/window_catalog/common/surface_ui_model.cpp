#include "surface_ui_model.h"
#include <algorithm>
#include "apptraverse/model_object_proxy.h"
#include "apptraverse/application_persistence.h"
#include "apptraverse/runtime_node.h"
#include "apptraverse/ui/ui_presented_window_composition.h"
#include "apptraverse/ui/ui_presented_window_lifecycle.h"
namespace apptraverse::example::surface_ui {
namespace {
APPTRAVERSE_REGISTER(SurfaceApplication);
APPTRAVERSE_REGISTER(SurfaceWindow);
APPTRAVERSE_REGISTER(SurfaceAction);
APPTRAVERSE_REGISTER(AddSurfaceUiEvent);
APPTRAVERSE_REGISTER(RemoveSurfaceUiEvent);
APPTRAVERSE_REGISTER(SelectSurfaceUiEvent);
APPTRAVERSE_REGISTER(OrderSurfaceUiEvent);
ui::UiLocalizationString::ptr Label(ae::Domain& d, Node& parent, std::string en, std::string ru, int language) {
  auto label = ui::UiLocalizationString::ptr::Create(ae::CreateWith{d});
  for (auto const& value : {en, ru}) {
    auto text = ui::UiString::ptr::Create(ae::CreateWith{d}.with_flags(ae::ObjFlags::kUnloadedByDefault));
    text->utf8 = value;
    text.Save();
    text.Reset();
    label->strings.push_back(text);
  }
  InitializeRuntimeNode(*label, parent);
  label->MaterializeLanguage(language);
  return label;
}
SurfaceWindow::ptr NewWindow(SurfaceApplication& app, std::uint32_t number) {
  auto& d = *app.domain;
  auto window = SurfaceWindow::ptr::Create(ae::CreateWith{d});
  window->number = number;
  auto offset = static_cast<std::int32_t>((number - 1) * 36);
  window->BootstrapFrame(120 + offset, 120 + offset, 360, 240);
  auto layout = ui::UiLayoutState::ptr::Create(ae::CreateWith{d});
  layout->Bootstrap(app.resources.definition, false, {});
  InitializeRuntimeNode(*layout, app);
  auto const suffix = app.v1 && number > 1 ? " [s" + std::to_string(number) + "]" : std::string{};
  auto title = Label(d, app, "Surface " + std::to_string(number) + suffix,
      "Поверхность " + std::to_string(number) + suffix, app.resources.locale->language_index());
  ui::ComposePresentedWindow(window, app.factories.window(d), title, app.resources.locale, layout, app.resources.device);
  for (auto role : {1001u, 1002u, 1003u}) {
    auto action = SurfaceAction::ptr::Create(ae::CreateWith{d});
    action->application = SurfaceApplication::ptr::MakeFromThis(&app);
    action->window_id = window.id();
    action->role = role;
    action->BootstrapSetEnabled(true);
    action->label = role == 1001 ? app.resources.add_label : role == 1002 ? app.resources.close_label : app.resources.language_label;
    action->presenter = app.factories.button(d);
    action->presenter->button = action;
    action->presenter->window_presenter = window->presenter;
    action->presenter->control_id = role;
    action->presenter->layout_x = action->presenter->layout_y = 0;
    action->presenter->layout_width = action->presenter->layout_height = 0;
    InitializeRuntimeNode(*action, app);
    window->actions.push_back(action);
  }
  InitializeRuntimeNode(*window, app);
  return window;
}
bool HasWindow(SurfaceApplication const& app, ae::ObjId id) {
  return std::any_of(app.windows.begin(), app.windows.end(), [id](auto const& w) { return w.id() == id; });
}
}
ui::UiLocalizationString::ptr MakeSurfaceLabel(ae::Domain& d, Node& parent, std::string en, std::string ru, int language) {
  return Label(d, parent, std::move(en), std::move(ru), language);
}
SurfaceWindow::ptr MakeSurfaceWindow(SurfaceApplication& app, std::uint32_t number) {
  return NewWindow(app, number);
}
ae::ObjId ApplicationId() { return ae::ObjId{0x793001}; }
void EnsureSurfaceUiRegistration() { (void)&g_apptraverse_registrar_SurfaceApplication; }
void SurfaceAction::HandlePushButtonClicked() {
  application->PerformAction(window_id, role);
}
void SurfaceAction::QueueClickFromUi(ModelObjectProxy& proxy) const {
  proxy.Invoke(application.id(), &SurfaceApplication::PerformAction, window_id, role);
}
void SurfaceApplication::PerformAction(ae::ObjId window_id, std::uint32_t role) {
  if (!HasWindow(*this, window_id)) return;
  if (role == 1001) Add();
  else if (role == 1002) Remove(window_id);
  else SwitchLanguage();
}
void SurfaceApplication::Apply(AddSurfaceUiEvent const& e) {
  windows.push_back(e.window);
  next_number = e.window->number + 1;
  current = e.window.id();
  order.insert(order.begin(), current);
  NoteMaterializedChange();
}
void SurfaceApplication::Apply(RemoveSurfaceUiEvent const& e) {
  std::erase_if(windows, [&](auto const& w) { return w.id() == e.window_id; });
  std::erase(order, e.window_id);
  next_number = 1;
  for (auto const& w : windows) next_number = std::max(next_number, w->number + 1);
  if (current == e.window_id) current = windows.empty() ? ae::ObjId{} : windows.back().id();
  NoteMaterializedChange();
}
void SurfaceApplication::Apply(SelectSurfaceUiEvent const& e) {
  current = e.window_id;
  NoteMaterializedChange();
}
void SurfaceApplication::Apply(OrderSurfaceUiEvent const& e) { order = e.ids; NoteMaterializedChange(); }
void SurfaceApplication::Add() {
  auto event = AddSurfaceUiEvent::ptr::Create(ae::CreateWith{*domain});
  event->window = NewWindow(*this, next_number);
  auto focus = event->window.id();
  Commit(event);
  focus_request = focus;
}
void SurfaceApplication::Remove(ae::ObjId id) {
  if (!HasWindow(*this, id)) return;
  auto event = RemoveSurfaceUiEvent::ptr::Create(ae::CreateWith{*domain});
  event->window_id = id;
  Commit(event);
  focus_request = current;
  if (windows.empty() && stop_when_empty) stop_when_empty();
}
void SurfaceApplication::Select(ae::ObjId id) {
  if (!HasWindow(*this, id) || current == id) return;
  auto event = SelectSurfaceUiEvent::ptr::Create(ae::CreateWith{*domain});
  event->window_id = id;
  Commit(event);
}
void SurfaceApplication::SetOrder(std::vector<std::uint32_t> ids) {
  std::vector<ae::ObjId> complete;
  for (auto raw : ids) {
    auto id = ae::ObjId{raw};
    if (HasWindow(*this, id) && std::find(complete.begin(), complete.end(), id) == complete.end()) complete.push_back(id);
  }
  for (auto const& w : windows)
    if (std::find(complete.begin(), complete.end(), w.id()) == complete.end()) complete.push_back(w.id());
  if (order == complete) return;
  auto event = OrderSurfaceUiEvent::ptr::Create(ae::CreateWith{*domain});
  event->ids = std::move(complete);
  Commit(event);
}
void SurfaceApplication::SwitchLanguage() {
  auto language = 1 - resources.locale->language_index();
  for (auto label : {resources.add_label, resources.close_label, resources.language_label}) label->MaterializeLanguage(language);
  for (auto const& w : windows) w->presenter->title->MaterializeLanguage(language);
  resources.locale->SubmitLanguageIndexFromUi(language);
}
void PrepareSurfaceUi(SurfaceApplication& app, SurfaceFactories factories) {
  app.factories = std::move(factories);
  app.resources.locale.Load();
  app.resources.device.Load();
  app.resources.definition.Load();
  auto language = app.resources.locale->language_index();
  for (auto label : {app.resources.add_label, app.resources.close_label, app.resources.language_label}) {
    label.Load(); label->MaterializeLanguage(language);
  }
  for (auto& w : app.windows) {
    w.Load();
    ui::LoadPresentedWindow(w);
    w->presenter->title.Load();
    w->presenter->title->MaterializeLanguage(language);
    for (auto const& a : w->actions) { a.Load(); a->presenter.Load(); }
  }
}
int SaveSurfaceUi(SurfaceApplication& app) {
  CompactApplicationNodesForUserOverlay(app);
  ae::DomainGraph graph{app.domain};
  return graph.SaveRoot(SurfaceApplication::ptr::MakeFromThis(&app).cached(), app.obj_id).IsErr() ? 1 : 0;
}
} // namespace apptraverse::example::surface_ui
