#ifndef APPTRAVERSE_SURFACES_MAC_PRESENTERS_H_
#define APPTRAVERSE_SURFACES_MAC_PRESENTERS_H_

#include "apptraverse/object_macros.h"

#include "desktop_surface_presenter.h"

namespace apptraverse {

void EnsureMacSurfacePresenterRegistration();

// AppKit NSWindow per Surface, SwiftUI content (SurfaceContentView.swift).
// Objective-C types stay in .mm (void* bridges).
// Coordinate contract: Surface desktop_* = top-left outer frame. Conversion to
// AppKit bottom-left frames uses the primary screen only (no multi-monitor).
class MacSurfacePresenter : public DesktopSurfacePresenter {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::surfaces::MacSurfacePresenter",
      MacSurfacePresenter, DesktopSurfacePresenter, 0)

 protected:
  MacSurfacePresenter() = default;

 public:
  explicit MacSurfacePresenter(ae::ObjProp prop)
      : DesktopSurfacePresenter{prop} {}

  AE_OBJECT_REFLECT()

  void OnLoad() override;
  void OnModelChanged() override;
  void OnUnload() override;

  // Snapshot NSWindow frame → common top-left bounds → UpdateModelBounds.
  void QueueCurrentBounds();

  // Bridged NSWindow* / SurfaceWindowDelegate* (ARC retained). Controls live
  // in the SwiftUI content view and are not owned here.
  void* window{nullptr};
  void* window_delegate{nullptr};
};

// presentation_host → MacApp::RequestApplicationStop (defined in mac_app.mm).
void MacRequestApplicationStop(void* presentation_host);

}  // namespace apptraverse

#endif  // APPTRAVERSE_SURFACES_MAC_PRESENTERS_H_
