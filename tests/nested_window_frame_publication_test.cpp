#include <cstdlib>
#include <deque>
#include <iostream>
#include <optional>
#include <utility>
#include <vector>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"

#include "apptraverse/application_ui_session.h"
#include "apptraverse/model_object_proxy.h"
#include "apptraverse/node_for.h"
#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/object_serialization.h"
#include "apptraverse/runtime_node.h"
#include "apptraverse/ui/ui_desktop_window.h"
#include "apptraverse/ui/ui_presented_window_composition.h"
#include "apptraverse/ui/ui_presenters.h"
#include "window_title_fixture.h"

namespace apptraverse::test {
namespace {

#define CHECK(condition)                                                      \
  do {                                                                      \
    if (!(condition)) {                                                     \
      std::cerr << "CHECK failed: " #condition << " at " << __FILE__ << ':'  \
                << __LINE__ << '\n';                                        \
      std::exit(1);                                                         \
    }                                                                       \
  } while (false)

struct Frame {
  std::int32_t x;
  std::int32_t y;
  std::int32_t width;
  std::int32_t height;
};

bool operator==(Frame const& a, Frame const& b) {
  return a.x == b.x && a.y == b.y && a.width == b.width &&
         a.height == b.height;
}

Frame RestoreFrame(ui::UiDesktopWindow const& window) {
  return {window.x(), window.y(), window.restore_client_width(),
          window.restore_client_height()};
}

// The native boundary is recorded only in this test presenter. Production
// publication, UI session and model-proxy code are used without test hooks.
class NestedFramePresenter : public ui::UiDesktopWindowPresenter {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::NestedFramePresenter",
                          NestedFramePresenter, ui::UiDesktopWindowPresenter, 0)

 protected:
  NestedFramePresenter() = default;

 public:
  explicit NestedFramePresenter(ae::ObjProp prop)
      : ui::UiDesktopWindowPresenter{prop} {}

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_);
  }

  Frame native_frame{};
  std::vector<Frame> requests;
  unsigned loads{0};

  void OnLoad() override {
    ++loads;
    native_frame = RestoreFrame(*desktop_window());
  }

  void ApplyRequestedFrameFromMirror() override {
    CHECK(presentation_loaded);
    native_frame = RestoreFrame(*desktop_window());
    requests.push_back(native_frame);
  }

  void NativeFrameChanged(Frame frame, bool updates_restore = true) {
    native_frame = frame;
    ReportNativeFrameToModel(frame.x, frame.y, frame.width, frame.height,
                             updates_restore);
  }
};

class NestedFrameApplication : public NodeFor<NestedFrameApplication> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::NestedFrameApplication",
                          NestedFrameApplication, Node, 0)

 protected:
  NestedFrameApplication() = default;

 public:
  explicit NestedFrameApplication(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(windows), AE_MMBR(first_alias))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, windows, first_alias);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, windows, first_alias);
  }

  std::vector<ui::UiDesktopWindow::ptr> windows;
  ui::UiDesktopWindow::ptr first_alias;
};

APPTRAVERSE_REGISTER(NestedFramePresenter);
APPTRAVERSE_REGISTER(NestedFrameApplication);

struct Harness {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  NestedFrameApplication::ptr application;
  std::deque<ModelObjectProxy::ModelWork> work;
  ModelObjectProxy proxy;
  std::optional<ApplicationUiSession> session;

  explicit Harness(ui::UiDesktopWindow::InitialPlacement first_placement =
                       ui::UiDesktopWindow::InitialPlacement::Explicit)
      : proxy{[this](ModelObjectProxy::ModelWork next) {
          work.push_back(std::move(next));
        }} {
    application = NestedFrameApplication::ptr::Create(ae::CreateWith{domain});
    AddWindow(first_placement);
    AddWindow(ui::UiDesktopWindow::InitialPlacement::Explicit);
    application->first_alias = application->windows.front();
    InitializeRuntimeNode(*application);
    ByteSink initial;
    SerializeInitialPublication(*application, initial);
    session.emplace(initial.bytes, nullptr, proxy);
    CHECK(&session->root() != &*application);
    CHECK(session->root().domain != &domain);
    CHECK(Presenter(0).loads == 1 && Presenter(1).loads == 1);
  }

  void AddWindow(ui::UiDesktopWindow::InitialPlacement placement) {
    auto window = ui::UiDesktopWindow::ptr::Create(ae::CreateWith{domain});
    window->BootstrapFrame(50, 70, 640, 480, placement);
    auto presenter = NestedFramePresenter::ptr::Create(ae::CreateWith{domain});
    BuildEmptyWindowTitle(*presenter);
    ui::ComposePresentedWindow(window, presenter, presenter->title,
                               presenter->locale);
    InitializeRuntimeNode(*window);
    application->windows.push_back(std::move(window));
  }

  NestedFrameApplication& Mirror() {
    return static_cast<NestedFrameApplication&>(session->root());
  }

  NestedFramePresenter& Presenter(std::size_t index) {
    return static_cast<NestedFramePresenter&>(
        *Mirror().windows.at(index)->presenter);
  }

  void DrainModel() {
    while (!work.empty()) {
      auto next = std::move(work.front());
      work.pop_front();
      next(domain);
    }
  }

  void Publish(Node& root) {
    ByteSink publication;
    SerializeStructuralNodePublication(root, publication);
    session->ApplyStructural(publication.bytes);
  }

  void Publish() { Publish(*application); }

  void Request(std::size_t index, Frame frame) {
    application->windows.at(index)->RequestFrame(
        frame.x, frame.y, frame.width, frame.height);
  }
};

void TestNestedRequestsAndIndependentWindows() {
  Harness h;
  Frame const first{-90, -40, 720, 510};
  Frame const untouched = h.Presenter(1).native_frame;
  h.Request(0, first);

  // A plain graph snapshot must not consume the structural presentation intent.
  ByteSink snapshot;
  SerializeObjectGraphToBuffer(*h.application, snapshot);
  h.Publish();
  CHECK(h.Presenter(0).requests.size() == 1);
  CHECK(h.Presenter(0).native_frame == first);
  CHECK(h.Presenter(1).requests.empty());
  CHECK(h.Presenter(1).native_frame == untouched);
  CHECK(RestoreFrame(*h.Mirror().windows[0]) == first);
  h.Publish();
  CHECK(h.Presenter(0).requests.size() == 1);
  CHECK(h.Presenter(1).requests.empty());

  Frame const latest{-200, 60, 810, 620};
  Frame const second{300, -100, 510, 390};
  h.Request(0, {15, 25, 700, 500});
  h.Request(0, latest);
  h.Request(1, second);
  h.Publish();
  CHECK(h.Presenter(0).requests.size() == 2);
  CHECK(h.Presenter(0).requests.back() == latest);
  CHECK(h.Presenter(1).requests.size() == 1);
  CHECK(h.Presenter(1).requests.back() == second);
  h.Publish();
  CHECK(h.Presenter(0).requests.size() == 2);
  CHECK(h.Presenter(1).requests.size() == 1);
}

void TestRootWindowPublicationStillApplies() {
  Harness h;
  Frame const request{-150, 180, 650, 490};
  h.Request(1, request);
  h.Publish(*h.application->windows[1]);
  CHECK(h.Presenter(0).requests.empty());
  CHECK(h.Presenter(1).requests.size() == 1);
  CHECK(h.Presenter(1).native_frame == request);
  h.Publish();
  CHECK(h.Presenter(1).requests.size() == 1);
}

void TestNativeReportsNeverRequestNativePlacement() {
  Harness h;
  Frame const native{-300, -80, 760, 550};
  h.Presenter(0).NativeFrameChanged(native);
  CHECK(h.work.size() == 1);
  CHECK(!(RestoreFrame(*h.application->windows[0]) == native));
  h.DrainModel();
  h.Publish();
  CHECK(RestoreFrame(*h.Mirror().windows[0]) == native);
  CHECK(h.Presenter(0).native_frame == native);
  CHECK(h.Presenter(0).requests.empty());
  CHECK(h.Presenter(1).requests.empty());

  Frame const maximized{-1920, 0, 1920, 1040};
  h.Presenter(0).NativeFrameChanged(maximized, false);
  h.DrainModel();
  h.Publish();
  CHECK(RestoreFrame(*h.Mirror().windows[0]) == native);
  CHECK(h.Mirror().windows[0]->client_width() == maximized.width);
  CHECK(h.Mirror().windows[0]->client_height() == maximized.height);
  CHECK(h.Presenter(0).native_frame == maximized);
  CHECK(h.Presenter(0).requests.empty());
  h.Publish();
  CHECK(h.Presenter(0).requests.empty());
}

void TestScreenEventRequestsNestedPlacementOnce() {
  Harness h{ui::UiDesktopWindow::InitialPlacement::Centered};
  Frame const untouched = h.Presenter(1).native_frame;
  h.Presenter(0).NativeFrameChanged({12, 34, 640, 480});
  h.Presenter(0).SubmitScreenFromNative(-1200, 0, 1200, 900);
  CHECK(h.work.size() == 2);
  h.DrainModel();
  Frame const centered{-920, 210, 640, 480};
  CHECK(RestoreFrame(*h.application->windows[0]) == centered);
  h.Publish();
  CHECK(h.Presenter(0).requests.size() == 1);
  CHECK(h.Presenter(0).native_frame == centered);
  CHECK(h.Presenter(1).native_frame == untouched);
  CHECK(h.Presenter(1).requests.empty());
  h.Presenter(0).SubmitScreenFromNative(0, 0, 1920, 1080);
  h.DrainModel();
  h.Publish();
  CHECK(h.Presenter(0).requests.size() == 1);
  CHECK(h.Presenter(0).native_frame == centered);
}

void TestNewWindowUsesInitialLoadFrame() {
  Harness h;
  h.AddWindow(ui::UiDesktopWindow::InitialPlacement::Explicit);
  Frame const request{-60, 90, 610, 450};
  h.Request(2, request);
  h.Publish();
  CHECK(h.Presenter(2).loads == 1);
  CHECK(h.Presenter(2).requests.empty());
  CHECK(h.Presenter(2).native_frame == request);
  h.Publish();
  CHECK(h.Presenter(2).loads == 1);
  CHECK(h.Presenter(2).requests.empty());
}

}  // namespace
}  // namespace apptraverse::test

int main() {
  apptraverse::EnableNoninteractiveCrt();
  apptraverse::EnsureCoreObjectRegistration();
  apptraverse::ui::EnsureUiControlRegistration();
  using namespace apptraverse::test;
  TestNestedRequestsAndIndependentWindows();
  TestRootWindowPublicationStillApplies();
  TestNativeReportsNeverRequestNativePlacement();
  TestScreenEventRequestsNestedPlacementOnce();
  TestNewWindowUsesInitialLoadFrame();
  std::cout << "NESTED_WINDOW_FRAME_PUBLICATION ok\n";
}
