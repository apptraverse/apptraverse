#include "main_window_model.h"

#include "aether-objects/obj/domain.h"

namespace apptraverse {

void MainWindow::Apply(WindowChangedEvent const& event) {
  x = event.x;
  y = event.y;
  width = event.width;
  height = event.height;
  NoteMaterializedChange();
}

Application::ptr BuildMainWindowGraph(ae::Domain& domain, MainWindowPresenterFactory factory) {
  auto application = Application::ptr::Create(ae::CreateWith{domain}.with_id(
      main_window::ToObjId(main_window::ObjId::Application)));
  auto window = MainWindow::ptr::Create(ae::CreateWith{domain}.with_id(
      main_window::ToObjId(main_window::ObjId::MainWindow)));
  ae::ObjId presenter_id{main_window::ToObjId(main_window::ObjId::MainWindowPresenter)};
  auto presenter = factory ? factory(domain, presenter_id) : MainWindowPresenter::ptr::Create(ae::CreateWith{domain}.with_id(presenter_id));
  window->x = main_window::kDefaultX;
  window->y = main_window::kDefaultY;
  window->width = main_window::kDefaultWidth;
  window->height = main_window::kDefaultHeight;
  window->presenter = presenter;
  presenter->window = window;
  application->main_window = window;
  return application;
}

}  // namespace apptraverse
