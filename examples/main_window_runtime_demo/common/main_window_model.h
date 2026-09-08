#ifndef APPTRAVERSE_MAIN_WINDOW_MODEL_H_
#define APPTRAVERSE_MAIN_WINDOW_MODEL_H_

#include <cstdint>
#include <stdexcept>

#include "aether-objects/obj/obj.h"

#include "apptraverse/node_for.h"
#include "apptraverse/object_macros.h"

#include "main_window_ids.h"

namespace apptraverse {

class MainWindow : public NodeFor<MainWindow> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::example::MainWindow", MainWindow, Node,
                           1)

 protected:
  MainWindow() = default;

 public:
  explicit MainWindow(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(x), AE_MMBR(y), AE_MMBR(width), AE_MMBR(height),
                    AE_MMBR(dpi))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv&) {
    throw std::runtime_error("MainWindow v0 is not supported");
  }

  template <typename Dnv>
  void Load(ae::Version<1>, Dnv& dnv) {
    Node::Load(ae::Version<1>{}, dnv);
    dnv(x, y, width, height, dpi);
  }

  template <typename Dnv>
  void Save(ae::Version<1>, Dnv& dnv) const {
    Node::Save(ae::Version<1>{}, dnv);
    dnv(x, y, width, height, dpi);
  }

  std::int32_t x{main_window::kDefaultX};
  std::int32_t y{main_window::kDefaultY};
  std::int32_t width{main_window::kDefaultWidth};
  std::int32_t height{main_window::kDefaultHeight};
  std::int32_t dpi{main_window::kDefaultDpi};
};

class Application : public ae::Obj {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::example::Application", Application,
                           ae::Obj, 0)

 protected:
  Application() = default;

 public:
  explicit Application(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(main_window))

  MainWindow::ptr main_window;
};

void EnsureMainWindowRegistration();

Application::ptr BuildMainWindowGraph(ae::Domain& domain);

}  // namespace apptraverse

#endif  // APPTRAVERSE_MAIN_WINDOW_MODEL_H_
