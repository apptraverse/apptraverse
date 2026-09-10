#import <UIKit/UIKit.h>

#include <cmath>

#include "apptraverse/object_macros.h"

#include "ios_presenters.h"

namespace apptraverse {
namespace {

APPTRAVERSE_REGISTER(IOSSurfacePresenter);

}  // namespace

void EnsureIOSSurfacePresenterRegistration() {
  EnsureMobileSurfacePresenterRegistration();
  (void)&g_apptraverse_registrar_IOSSurfacePresenter;
}

void IOSSurfacePresenter::OnLoad() {
  // The page is the caption itself; the host sets its frame when it lays the
  // pager out in Surfaces order.
  UILabel* page = [[UILabel alloc] initWithFrame:CGRectZero];
  page.text = [NSString stringWithUTF8String:PageTitle().c_str()];
  page.textAlignment = NSTextAlignmentCenter;
  page.font = [UIFont systemFontOfSize:28.0];
  // Distinct tint per Surface so a swipe is visible in the simulator.
  page.backgroundColor =
      [UIColor colorWithHue:std::fmod(surface->number * 0.17, 1.0)
                 saturation:0.18
                 brightness:1.0
                      alpha:1.0];

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
