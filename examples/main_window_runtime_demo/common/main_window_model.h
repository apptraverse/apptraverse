#ifndef APPTRAVERSE_MAIN_WINDOW_MODEL_H_
#define APPTRAVERSE_MAIN_WINDOW_MODEL_H_

#include <cstdint>
#include <stdexcept>

#include "aether-objects/obj/obj.h"

#include "apptraverse/event_for.h"
#include "apptraverse/node_for.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/presenter.h"

#include "main_window_ids.h"

namespace apptraverse {

class MainWindowPresenter;
class WindowChangedEvent;

class MainWindow : public NodeFor<MainWindow> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::example::MainWindow", MainWindow, Node,
                           4)

 protected:
  MainWindow() = default;

 public:
  explicit MainWindow(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(x), AE_MMBR(y), AE_MMBR(width), AE_MMBR(height),
                    AE_MMBR(presenter))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv&) {
    throw std::runtime_error("MainWindow v0 is not supported");
  }

  template <typename Dnv>
  void Load(ae::Version<1>, Dnv&) {
    throw std::runtime_error("MainWindow v1 is not supported");
  }

  template <typename Dnv>
  void Load(ae::Version<2>, Dnv&) {
    throw std::runtime_error("MainWindow v2 is not supported");
  }

  template <typename Dnv>
  void Load(ae::Version<3>, Dnv& dnv) {
    Node::Load(ae::Version<1>{}, dnv);
    dnv(x, y, width, height, presenter);
  }

  template <typename Dnv>
  void Load(ae::Version<4>, Dnv& dnv) {
    Node::Load(ae::Version<2>{}, dnv);
    dnv(x, y, width, height, presenter);
  }

  template <typename Dnv>
  void Save(ae::Version<4>, Dnv& dnv) const {
    Node::Save(ae::Version<2>{}, dnv);
    dnv(x, y, width, height, presenter);
  }

  std::int32_t x{main_window::kDefaultX};
  std::int32_t y{main_window::kDefaultY};
  std::int32_t width{main_window::kDefaultWidth};
  std::int32_t height{main_window::kDefaultHeight};
  ae::ObjPtr<MainWindowPresenter> presenter;

  void Apply(WindowChangedEvent const& event);
};

class MainWindowPresenter : public Presenter {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::example::MainWindowPresenter",
                           MainWindowPresenter, Presenter, 0)

 protected:
  MainWindowPresenter() = default;

 public:
  explicit MainWindowPresenter(ae::ObjProp prop) : Presenter{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(window))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, window);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, window);
  }

  MainWindow::ptr window;

  // GUI-thread correlation. Not serialized, not journaled. Model-side
  // presenters leave these at 0.
  std::uint64_t last_submitted_window_change_sequence{0};
  std::uint64_t last_acknowledged_window_change_sequence{0};

  // A publication may update the mirror and still be stale relative to newer
  // native input. Only an acknowledgement of every submitted command may
  // drive native presentation.
  bool WindowChangePublicationIsCurrent() const {
    return last_acknowledged_window_change_sequence >=
           last_submitted_window_change_sequence;
  }
};

// Position and size as one outer-window rectangle. Model Domain only.
class WindowChangedEvent : public EventFor<MainWindow, WindowChangedEvent> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::example::WindowChangedEvent",
                           WindowChangedEvent, Event, 0)

 protected:
  WindowChangedEvent() = default;

 public:
  explicit WindowChangedEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(x), AE_MMBR(y), AE_MMBR(width), AE_MMBR(height))

  std::int32_t x{0};
  std::int32_t y{0};
  std::int32_t width{0};
  std::int32_t height{0};
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

Application::ptr BuildMainWindowGraph(ae::Domain& domain);

}  // namespace apptraverse

#endif  // APPTRAVERSE_MAIN_WINDOW_MODEL_H_
