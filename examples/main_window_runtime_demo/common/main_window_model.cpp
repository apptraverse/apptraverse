#include "main_window_model.h"

#include "aether-objects/obj/domain.h"

#include "apptraverse/object_macros.h"

namespace apptraverse {
namespace {

APPTRAVERSE_REGISTER(MainWindow);
APPTRAVERSE_REGISTER(Application);

}  // namespace

void EnsureMainWindowRegistration() { EnsureObjectRegistration(); }

Application::ptr BuildMainWindowGraph(ae::Domain& domain) {
  auto application = Application::ptr::Create(ae::CreateWith{domain}.with_id(
      main_window::ToObjId(main_window::ObjId::Application)));
  auto window = MainWindow::ptr::Create(ae::CreateWith{domain}.with_id(
      main_window::ToObjId(main_window::ObjId::MainWindow)));
  window->x = main_window::kDefaultX;
  window->y = main_window::kDefaultY;
  window->width = main_window::kDefaultWidth;
  window->height = main_window::kDefaultHeight;
  application->main_window = window;
  return application;
}

}  // namespace apptraverse
