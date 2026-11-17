#include "runtime_ui_model.h"
#include <algorithm>
#include "apptraverse/model_object_proxy.h"
#include "apptraverse/runtime_node.h"
#include "apptraverse/ui/ui_presented_window_lifecycle.h"
namespace apptraverse::example::runtime_ui {
namespace {
APPTRAVERSE_REGISTER(RuntimeApplication);
APPTRAVERSE_REGISTER(RuntimeButton);
APPTRAVERSE_REGISTER(RuntimeItemLayoutState);
APPTRAVERSE_REGISTER(AddRuntimeItemEvent);
APPTRAVERSE_REGISTER(RemoveRuntimeItemEvent);
}  // namespace
ui::UiLocalizationString::ptr MakeRuntimeUiLabel(ae::Domain& domain,
                                                 Node& parent,
                                                 std::string text) {
  auto string = ui::UiString::ptr::Create(ae::CreateWith{domain});
  string->utf8 = std::move(text);
  auto label = ui::UiLocalizationString::ptr::Create(ae::CreateWith{domain});
  label->strings = {string};
  InitializeRuntimeNode(*label, parent);
  label->MaterializeLanguage(0);
  return label;
}
RuntimeButton::ptr MakeRuntimeUiButton(RuntimeApplication& app,
                                       std::uint32_t number) {
  auto button = RuntimeButton::ptr::Create(ae::CreateWith{*app.domain});
  button->application = RuntimeApplication::ptr::MakeFromThis(&app);
  button->number = number;
  button->BootstrapSetEnabled(true);
  button->label = MakeRuntimeUiLabel(
      *app.domain, *button,
      number ? "Remove item " + std::to_string(number) : "Add item");
  button->presenter = app.factories.button(*app.domain);
  button->presenter->button = button;
  button->presenter->window_presenter = app.window->presenter;
  button->presenter->layout_x = 0;
  button->presenter->layout_y = 0;
  button->presenter->layout_width = 0;
  button->presenter->layout_height = 0;
  button->presenter->control_id = number ? 1000 + number : 100;
  InitializeRuntimeNode(*button, *app.window);
  return button;
}
ui::UiLayoutNode const& RuntimeItemLayoutState::EffectiveRoot() const {
  auto const& authored = ui::UiLayoutState::EffectiveRoot();
  // The immutable definition guarantees these two semantic template slots.
  auto const& add_template = definition->root.children[0];
  auto const& item_template = definition->root.children[1];
  projected_root_ = authored;
  projected_root_.children.clear();
  auto authored_slot = [&](auto&& self, ui::UiLayoutNode const& node,
                           std::uint32_t id) -> ui::UiLayoutNode const* {
    if (node.flow == ui::UiLayoutFlow::kLeaf && node.control_id == id)
      return &node;
    for (auto const& child : node.children) {
      if (auto slot = self(self, child, id)) return slot;
    }
    return nullptr;  // An override need not specify every live/repeated slot.
  };
  auto append = [&](ui::UiLayoutNode const& prototype, std::uint32_t id) {
    auto slot = authored_slot(authored_slot, authored, id);
    auto node = slot ? *slot : prototype;
    node.control_id = id;
    projected_root_.children.push_back(std::move(node));
  };
  append(add_template, application->add->presenter->control_id);
  for (auto const& item : application->items) {
    append(item_template, item->presenter->control_id);
  }
  return projected_root_;
}
void EnsureRuntimeUiRegistration() {
  (void)&g_apptraverse_registrar_RuntimeApplication;
}
ae::ObjId RuntimeApplicationId() { return ae::ObjId{0x792001}; }
void RuntimeButton::HandlePushButtonClicked() {
  if (number == 0)
    application->AddItem();
  else
    application->RemoveItem(obj_id);
}
void RuntimeButton::QueueClickFromUi(ModelObjectProxy& proxy) const {
  if (number == 0)
    proxy.Invoke(application.id(), &RuntimeApplication::AddItem);
  else
    proxy.Invoke(application.id(), &RuntimeApplication::RemoveItem, obj_id);
}
void RuntimeApplication::Apply(AddRuntimeItemEvent const& event) {
  items.push_back(event.item);
  NoteMaterializedChange();
}
void RuntimeApplication::Apply(RemoveRuntimeItemEvent const& event) {
  std::erase_if(items,
                [&](auto const& item) { return item.id() == event.item_id; });
  NoteMaterializedChange();
}
void RuntimeApplication::AddItem() {
  std::uint32_t number = 1;
  for (auto const& item : items) number = std::max(number, item->number + 1);
  auto event = AddRuntimeItemEvent::ptr::Create(ae::CreateWith{*domain});
  event->item = MakeRuntimeUiButton(*this, number);
  Commit(std::move(event));
}
void RuntimeApplication::RemoveItem(ae::ObjId id) {
  if (std::none_of(items.begin(), items.end(),
                   [&](auto const& item) { return item.id() == id; }))
    return;
  auto event = RemoveRuntimeItemEvent::ptr::Create(ae::CreateWith{*domain});
  event->item_id = id;
  Commit(std::move(event));
}
void PrepareRuntimeUi(RuntimeApplication& app,
                      RuntimePresenterFactories factories) {
  app.factories = std::move(factories);
  ui::LoadPresentedWindow(app.window);
  app.window->presenter->title.Load();
  app.window->presenter->title->MaterializeLanguage(0);
  app.window->presenter->locale.Load();
  if (app.add.is_valid()) {
    app.add.Load();
    app.add->label.Load();
    app.add->label->MaterializeLanguage(0);
    app.add->presenter.Load();
    for (auto const& item : app.items) {
      item.Load();
      item->label.Load();
      item->label->MaterializeLanguage(0);
      item->presenter.Load();
    }
  }
}
}  // namespace apptraverse::example::runtime_ui
