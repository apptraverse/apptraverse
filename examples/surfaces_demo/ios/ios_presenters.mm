#import <UIKit/UIKit.h>

#include <cmath>

#include "apptraverse/object_macros.h"

#include "ios_presenters.h"
#include "ios_surface_content.h"

namespace apptraverse {
namespace {

APPTRAVERSE_REGISTER(IOSSurfacePresenter);

}  // namespace

void EnsureIOSSurfacePresenterRegistration() {
  EnsureMobileSurfacePresenterRegistration();
  (void)&g_apptraverse_registrar_IOSSurfacePresenter;
}

void IOSSurfacePresenter::OnLoad() {
  // UIKit owns the page container: the host sets its frame when it lays the
  // pager out in Surfaces order. SwiftUI only draws inside it.
  UIView* page = [[UIView alloc] initWithFrame:CGRectZero];
  // Distinct tint per Surface so a swipe is visible in the simulator.
  ApptraverseInstallIOSSurfacePage(
      page, [NSString stringWithUTF8String:PageTitle().c_str()],
      std::fmod(surface->number * 0.17, 1.0));

  page_view = (__bridge_retained void*)page;
  IOSAttachPage(presentation_host, page_view);
}

void IOSSurfacePresenter::OnUnload() {
  IOSDetachPage(page_view);
  UIView* page = (__bridge_transfer UIView*)page_view;
  page_view = nullptr;
  (void)page;
}

}  // namespace apptraverse
