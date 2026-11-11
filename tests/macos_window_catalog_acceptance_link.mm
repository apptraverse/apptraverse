#import "macos_window_catalog_acceptance.h"

#include "apptraverse/model_object_proxy.h"
#include "apptraverse/platform/macos/macos_ui_presenters.h"

void ATMacAcceptanceRequestFrame(uint64_t token, int32_t x, int32_t y,
                                 int32_t width, int32_t height) {
  auto& presenter = *reinterpret_cast<
      apptraverse::ui::macos::MacDesktopWindowPresenter*>(
      static_cast<uintptr_t>(token));
  presenter.model_proxy->Invoke(
      presenter.desktop_window()->obj_id,
      &apptraverse::ui::UiDesktopWindow::RequestFrame,
      x, y, width, height);
}

#include <vector>
#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "apptraverse/runtime_node.h"

BOOL ATMacAcceptanceCheckLateNativeReports(void) {
  // Separate model/mirror domains: removing the model target must not be
  // masked by the live UI presenter's reference to its own mirror object.
  ae::RamDomainStorage model_storage;
  ae::RamDomainStorage ui_storage;
  ae::Domain model_domain{model_storage};
  ae::Domain ui_domain{ui_storage};
  ae::ObjId const id{920001};
  auto mirror_window = apptraverse::ui::UiDesktopWindow::ptr::Create(
      ae::CreateWith{ui_domain}.with_id(id));
  auto presenter = apptraverse::ui::macos::MacDesktopWindowPresenter::ptr::Create(
      ae::CreateWith{ui_domain});
  mirror_window->BootstrapFrame(0, 0, 200, 160);
  mirror_window->presenter = {};
  presenter->window = mirror_window;
  presenter->title = {};
  presenter->locale = {};
  std::vector<apptraverse::ModelObjectProxy::ModelWork> pending;
  apptraverse::ModelObjectProxy proxy{
      [&pending](apptraverse::ModelObjectProxy::ModelWork work) {
        pending.push_back(std::move(work));
      }};
  presenter->model_proxy = &proxy;
  auto model_window = apptraverse::ui::UiDesktopWindow::ptr::Create(
      ae::CreateWith{model_domain}.with_id(id));
  model_window->BootstrapFrame(10, 20, 200, 160);
  model_window->presenter = {};
  presenter->ReportNativeFrameToModel(40, 50, 300, 240, true);
  presenter->ReportNativeScreenToModel(0, 0, 1200, 800);
  if (pending.size() != 2) return NO;
  model_window.Reset();
  if (model_domain.Find(id)) return NO;
  for (auto& work : pending) work(model_domain);
  pending.clear();
  if (model_domain.Find(id) || !model_storage.state.empty()) return NO;

  // The same route must still apply accepted reports for a live target.
  model_window = apptraverse::ui::UiDesktopWindow::ptr::Create(
      ae::CreateWith{model_domain}.with_id(id));
  model_window->BootstrapFrame(10, 20, 200, 160,
      apptraverse::ui::UiDesktopWindow::InitialPlacement::Centered);
  model_window->presenter = {};
  apptraverse::InitializeRuntimeNode(*model_window);
  model_window->SetImmediateEventCompaction(true);
  // Screen input must produce a model-owned centering decision and an explicit
  // native frame request, including signed screen coordinates.
  presenter->ReportNativeScreenToModel(100, -100, 1200, 800);
  for (auto& work : pending) work(model_domain);
  pending.clear();
  if (model_window->x() != 600 || model_window->y() != 220 ||
      !model_window->TakeFrameRequestPendingPublication()) return NO;
  presenter->ReportNativeFrameToModel(210, 220, 400, 260, true);
  for (auto& work : pending) work(model_domain);
  return model_window->x() == 210 && model_window->y() == 220 &&
      model_window->restore_client_width() == 400 &&
      model_window->restore_client_height() == 260;
}
