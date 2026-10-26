#include <cstdlib>
#include <iostream>
#include <set>
#include <utility>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"

#include "apptraverse/distill.h"
#include "apptraverse/embedded_distilled_domain.h"
#include "apptraverse/node.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/overlay_domain_storage.h"
#include "apptraverse/platform/windows/win_ui_presenters.h"
#include "apptraverse/ui/ui_desktop_window.h"
#include "apptraverse/ui/ui_window_presenter.h"

#include "embedded_distilled_minimal_app_supply.h"
#include "minimal_application.h"

namespace apptraverse::example::minimal {
namespace {

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"       \
                << __LINE__ << '\n';                                         \
      std::exit(1);                                                          \
    }                                                                        \
  } while (false)

bool HasEmbeddedLayer(EmbeddedDistilledGraph const& graph, ae::ObjId id,
                      std::uint32_t class_id, std::uint8_t version = 0) {
  for (std::size_t i = 0; i < graph.layer_count; ++i) {
    EmbeddedDistilledLayerRecord const& layer = graph.layers[i];
    if (layer.object_id == id.id() && layer.class_id == class_id &&
        layer.version == version) {
      return true;
    }
  }
  return false;
}

bool HasRamUserLayer(ae::RamDomainStorage const& user, ae::ObjId id,
                     std::uint32_t class_id, std::uint8_t version) {
  auto const obj_it = user.state.find(id);
  if (obj_it == user.state.end() || !obj_it->second.has_value()) {
    return false;
  }
  auto const class_it = obj_it->second->find(class_id);
  if (class_it == obj_it->second->end()) {
    return false;
  }
  return class_it->second.count(version) != 0;
}

void TestEmbeddedDescriptor() {
  EmbeddedDistilledGraph const& graph =
      embedded_distilled::minimal::kEmbeddedGraph;
  CHECK(graph.root_object_id == MinimalApplicationRootId().id());
  CHECK(graph.supply_constant_ids.count == 0);
  CHECK(graph.supply_constant_ids.ids == nullptr);

  for (std::size_t i = 0; i < graph.layer_count; ++i) {
    for (std::size_t j = i + 1; j < graph.layer_count; ++j) {
      EmbeddedDistilledLayerRecord const& a = graph.layers[i];
      EmbeddedDistilledLayerRecord const& b = graph.layers[j];
      CHECK(a.object_id != b.object_id || a.class_id != b.class_id ||
            a.version != b.version);
    }
  }

  CHECK(HasEmbeddedLayer(graph, MinimalApplicationRootId(),
                         MinimalApplication::kClassId));
  CHECK(HasEmbeddedLayer(graph, MinimalMainWindowId(),
                         ui::UiDesktopWindow::kClassId));
  CHECK(HasEmbeddedLayer(graph, MinimalPresenterId(),
                         ui::windows::WinDesktopWindowPresenter::kClassId));
}

void TestColdLoadFromEmbeddedBacking() {
  ae::RamDomainStorage backing;
  InstallEmbeddedDistilledBacking(
      backing, embedded_distilled::minimal::kEmbeddedGraph);
  ae::RamDomainStorage::ObjClassData const backing_snapshot = backing.state;

  ae::RamDomainStorage user;
  OverlayDomainStorage overlay{
      user, backing,
      EmbeddedSupplyConstantIdSet(embedded_distilled::minimal::kEmbeddedGraph)};
  ae::Domain domain{overlay};

  MinimalApplication::ptr loaded =
      LoadApplication<MinimalApplication>(domain, MinimalApplicationRootId());
  ae::Ptr<MinimalApplication> app = loaded.Load();

  CHECK(app->obj_id == MinimalApplicationRootId());
  CHECK(app->base.is_valid());
  CHECK(app->main_window.id() == MinimalMainWindowId());
  ae::Ptr<ui::UiDesktopWindow> window = app->main_window.Load();
  CHECK(window->presenter.id() == MinimalPresenterId());
  ae::Ptr<ui::UiWindowPresenter> presenter = window->presenter.Load();
  CHECK(presenter->window.id() == MinimalMainWindowId());
  CHECK(!presenter->title.is_valid());
  CHECK(!presenter->locale.is_valid());
  CHECK(window->x() == 120);
  CHECK(window->y() == 80);
  CHECK(window->client_width() == 640);
  CHECK(window->client_height() == 480);
  CHECK(user.state.empty());
  CHECK(backing.state == backing_snapshot);
}

void TestUserOverlayPersistence() {
  ae::RamDomainStorage backing;
  InstallEmbeddedDistilledBacking(
      backing, embedded_distilled::minimal::kEmbeddedGraph);
  ae::RamDomainStorage user;
  OverlayDomainStorage overlay{
      user, backing,
      EmbeddedSupplyConstantIdSet(embedded_distilled::minimal::kEmbeddedGraph)};

  {
    ae::Domain domain{overlay};
    MinimalApplication::ptr loaded = LoadApplication<MinimalApplication>(
        domain, MinimalApplicationRootId());
    ae::DomainGraph save_graph{&domain};
    CHECK(save_graph.SaveRoot(loaded.Load(), MinimalApplicationRootId())
              .IsOk());
    CHECK(user.state.empty());
  }

  ae::RamDomainStorage::ObjClassData user_after_change;
  {
    ae::Domain domain{overlay};
    MinimalApplication::ptr loaded = LoadApplication<MinimalApplication>(
        domain, MinimalApplicationRootId());
    ae::Ptr<MinimalApplication> app = loaded.Load();
    ae::Ptr<ui::UiDesktopWindow> window = app->main_window.Load();
    auto ev = ui::SetUiDesktopWindowFrameEvent::ptr::Create(
        ae::CreateWith{domain});
    ev->x = 140;
    ev->y = 90;
    ev->client_width = 800;
    ev->client_height = 600;
    window->Commit(std::move(ev));
    ae::DomainGraph save_graph{&domain};
    CHECK(save_graph.SaveRoot(app, MinimalApplicationRootId()).IsOk());
    CHECK(!user.state.empty());
    user_after_change = user.state;
  }

  ae::RamDomainStorage fresh_backing;
  InstallEmbeddedDistilledBacking(
      fresh_backing, embedded_distilled::minimal::kEmbeddedGraph);
  ae::RamDomainStorage replay_user;
  replay_user.state = user_after_change;
  OverlayDomainStorage replay_overlay{
      replay_user, fresh_backing,
      EmbeddedSupplyConstantIdSet(embedded_distilled::minimal::kEmbeddedGraph)};
  ae::Domain replay_domain{replay_overlay};
  MinimalApplication::ptr replay_loaded = LoadApplication<MinimalApplication>(
      replay_domain, MinimalApplicationRootId());
  ae::Ptr<ui::UiDesktopWindow> replay_window =
      replay_loaded.Load()->main_window.Load();
  CHECK(replay_window->x() == 140);
  CHECK(replay_window->y() == 90);
  CHECK(replay_window->client_width() == 800);
  CHECK(replay_window->client_height() == 600);
}

}  // namespace
}  // namespace apptraverse::example::minimal

int main() {
  apptraverse::EnsureObjectRegistration();
  apptraverse::ui::EnsureUiPresenterRegistration();
  apptraverse::ui::windows::EnsureWinUiPresenterRegistration();
  apptraverse::example::minimal::EnsureMinimalApplicationRegistration();
  apptraverse::example::minimal::TestEmbeddedDescriptor();
  apptraverse::example::minimal::TestColdLoadFromEmbeddedBacking();
  apptraverse::example::minimal::TestUserOverlayPersistence();
  std::cout << "minimal_app_embedded_export_test OK\n";
  return 0;
}
