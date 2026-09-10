#import <UIKit/UIKit.h>

#include <algorithm>
#include <utility>

#include "apptraverse/object_serialization.h"

#include "ios_app.h"

@interface SurfacesRootViewController : UIViewController <UIScrollViewDelegate>
@property(nonatomic, assign) apptraverse::IOSApp* app;
@property(nonatomic, strong) UIScrollView* pager;
@property(nonatomic, strong) UILabel* loading;
@property(nonatomic, strong) UIButton* addButton;
@property(nonatomic, strong) UIButton* removeButton;
@end

@implementation SurfacesRootViewController

- (void)viewDidLoad {
  [super viewDidLoad];
  self.view.backgroundColor = [UIColor systemBackgroundColor];

  self.pager = [[UIScrollView alloc] initWithFrame:CGRectZero];
  self.pager.pagingEnabled = YES;
  self.pager.showsHorizontalScrollIndicator = NO;
  self.pager.delegate = self;
  self.pager.hidden = YES;
  [self.view addSubview:self.pager];

  self.loading = [[UILabel alloc] initWithFrame:CGRectZero];
  self.loading.text = @"Loading";
  self.loading.textAlignment = NSTextAlignmentCenter;
  self.loading.font = [UIFont systemFontOfSize:24.0];
  [self.view addSubview:self.loading];

  self.addButton = [UIButton buttonWithType:UIButtonTypeSystem];
  [self.addButton setTitle:@"Add" forState:UIControlStateNormal];
  [self.addButton addTarget:self
                     action:@selector(onAdd:)
           forControlEvents:UIControlEventTouchUpInside];
  self.addButton.hidden = YES;
  [self.view addSubview:self.addButton];

  self.removeButton = [UIButton buttonWithType:UIButtonTypeSystem];
  [self.removeButton setTitle:@"Remove current"
                     forState:UIControlStateNormal];
  [self.removeButton addTarget:self
                        action:@selector(onRemoveCurrent:)
              forControlEvents:UIControlEventTouchUpInside];
  self.removeButton.hidden = YES;
  [self.view addSubview:self.removeButton];
}

- (void)viewDidLayoutSubviews {
  [super viewDidLayoutSubviews];
  CGSize const size = self.view.bounds.size;
  UIEdgeInsets const safe = self.view.safeAreaInsets;
  CGFloat const bar_height = 56.0;
  CGFloat const pager_height =
      size.height - safe.top - safe.bottom - bar_height;
  self.pager.frame = CGRectMake(0, safe.top, size.width, pager_height);
  self.loading.frame = self.pager.frame;

  CGFloat const bar_y = safe.top + pager_height;
  self.addButton.frame = CGRectMake(16, bar_y, 96, bar_height);
  self.removeButton.frame =
      CGRectMake(size.width - 196, bar_y, 180, bar_height);

  self.app->LayoutPages();
}

- (void)onAdd:(id)sender {
  (void)sender;
  self.app->AddCurrentClick();
}

- (void)onRemoveCurrent:(id)sender {
  (void)sender;
  // Disabled while a single Surface is left, so this is always removable.
  self.app->RemoveCurrentClick();
}

- (void)scrollViewDidEndDecelerating:(UIScrollView*)scrollView {
  CGFloat const width = scrollView.bounds.size.width;
  self.app->NoteVisiblePage(static_cast<std::size_t>(
      scrollView.contentOffset.x / width + 0.5));
}

- (void)scrollViewDidEndScrollingAnimation:(UIScrollView*)scrollView {
  CGFloat const width = scrollView.bounds.size.width;
  self.app->NoteVisiblePage(static_cast<std::size_t>(
      scrollView.contentOffset.x / width + 0.5));
}

@end

namespace apptraverse {

void IOSAttachPage(void* presentation_host, void* page_view) {
  auto* app = static_cast<IOSApp*>(presentation_host);
  SurfacesRootViewController* controller =
      (__bridge SurfacesRootViewController*)app->root_controller_;
  UIView* page = (__bridge UIView*)page_view;
  [controller.pager addSubview:page];
}

void IOSDetachPage(void* page_view) {
  UIView* page = (__bridge UIView*)page_view;
  [page removeFromSuperview];
}

void IOSApp::Start(std::filesystem::path const& state_dir) {
  UIWindow* window =
      [[UIWindow alloc] initWithFrame:[[UIScreen mainScreen] bounds]];
  SurfacesRootViewController* controller =
      [[SurfacesRootViewController alloc] init];
  controller.app = this;
  window.rootViewController = controller;
  // Host UIWindow / root controller live for the process lifetime; iOS has no
  // user-driven application close.
  window_ = (__bridge_retained void*)window;
  root_controller_ = (__bridge_retained void*)controller;
  [window makeKeyAndVisible];

  session_.state_dir = state_dir;
  model_proxy_.emplace([this](ModelObjectProxy::ModelWork work) {
    session_.Post(std::move(work));
  });

  model_thread_ = std::thread([this] {
    session_.Run([this](SurfacesPublicationKind kind) {
      if (kind == SurfacesPublicationKind::Initial) {
        dispatch_async(dispatch_get_main_queue(), ^{
          OnInitialPublished();
        });
      } else {
        dispatch_async(dispatch_get_main_queue(), ^{
          OnIncrementalPublished();
        });
      }
    });
  });
}

void IOSApp::Stop() {
  session_.RequestStop();
  model_thread_.join();
  // Terminate can arrive while the model is still loading.
  if (ui_application_) {
    UnloadPresenters(*ui_application_);
  }
  ui_application_ = {};
  ui_domain_.reset();
  model_proxy_.reset();
}

void IOSApp::OnInitialPublished() {
  std::vector<std::uint8_t> bytes;
  {
    std::lock_guard<std::mutex> lock{session_.mu};
    bytes = session_.channel.TakePublishedCopy();
  }
  session_.cv.notify_all();
  ui_domain_ = std::make_unique<ae::Domain>(ui_storage_);
  ByteSource in;
  in.data = bytes.data();
  in.size = bytes.size();
  auto ui_root = LoadInitialPublication(in, *ui_domain_, ui_storage_);
  ui_application_ = Application::ptr::MakeFromThis(
      static_cast<Application*>(ui_root.get()));
  InitializePresenters(*ui_application_, this, &*model_proxy_);
  page_count_ = ui_application_->surfaces->surfaces.size();
  RelayoutPages();
  EnsureModelCurrentSeeded();
  ApplyDesiredOffset();

  SurfacesRootViewController* controller =
      (__bridge SurfacesRootViewController*)root_controller_;
  controller.loading.hidden = YES;
  controller.pager.hidden = NO;
  controller.addButton.hidden = NO;
  controller.removeButton.hidden = NO;
  controller.removeButton.enabled =
      CurrentPresenter()->RemovableFromPager() ? YES : NO;
}

void IOSApp::OnIncrementalPublished() {
  std::vector<std::uint8_t> bytes;
  {
    std::lock_guard<std::mutex> lock{session_.mu};
    bytes = session_.channel.TakePublishedCopy();
  }
  session_.cv.notify_all();
  std::size_t const previous_count = page_count_;
  structural_apply_in_progress_ = true;
  ApplySurfacesStructural(bytes, *ui_application_, ui_storage_, this,
                          &*model_proxy_);
  structural_apply_in_progress_ = false;
  ReconcileDesiredAfterTopologyChange(previous_count);
  RelayoutPages();
  ApplyDesiredOffset();
  // Report current after topology settle so mobile_current tracks identity.
  if (auto presenter = CurrentPresenter()) {
    presenter->PageShown();
  }
  SurfacesRootViewController* controller =
      (__bridge SurfacesRootViewController*)root_controller_;
  controller.removeButton.enabled =
      CurrentPresenter()->RemovableFromPager() ? YES : NO;
}

void IOSApp::AddCurrentClick() { CurrentPresenter()->AddClick(); }

void IOSApp::RemoveCurrentClick() { CurrentPresenter()->RemoveClick(); }

void IOSApp::NoteVisiblePage(std::size_t index) {
  if (applying_model_current_) {
    return;
  }
  auto const& surfaces = ui_application_->surfaces->surfaces;
  if (index >= surfaces.size()) {
    return;
  }
  current_index_ = index;
  SetDesiredCurrent(surfaces[index]->obj_id.id());
  CurrentPresenter()->PageShown();
}

void IOSApp::LayoutPages() {
  if (ui_application_) {
    RelayoutPages();
    ApplyDesiredOffset();
  }
}

void IOSApp::RelayoutPages() {
  SurfacesRootViewController* controller =
      (__bridge SurfacesRootViewController*)root_controller_;
  UIScrollView* pager = controller.pager;
  CGSize const page_size = pager.bounds.size;

  auto const& surfaces = ui_application_->surfaces->surfaces;
  std::size_t const count = surfaces.size();
  for (std::size_t i = 0; i < count; ++i) {
    IOSSurfacePresenter::ptr presenter{surfaces[i]->presenter};
    UIView* page = (__bridge UIView*)presenter->page_view;
    page.frame = CGRectMake(page_size.width * static_cast<CGFloat>(i), 0,
                            page_size.width, page_size.height);
  }
  pager.contentSize =
      CGSizeMake(page_size.width * static_cast<CGFloat>(count),
                 page_size.height);
  page_count_ = count;
}

void IOSApp::ScrollToIndex(std::size_t index, bool animated) {
  SurfacesRootViewController* controller =
      (__bridge SurfacesRootViewController*)root_controller_;
  UIScrollView* pager = controller.pager;
  CGFloat const width = pager.bounds.size.width;
  current_index_ = index;
  applying_model_current_ = true;
  [pager setContentOffset:CGPointMake(width * static_cast<CGFloat>(index), 0)
                 animated:animated ? YES : NO];
  applying_model_current_ = false;
}

void IOSApp::ApplyDesiredOffset() {
  std::uint32_t const id = EffectiveCurrentId();
  if (id == 0) {
    return;
  }
  std::size_t const index = IndexOfSurfaceId(id);
  ScrollToIndex(index, false);
}

void IOSApp::SetDesiredCurrent(std::uint32_t surface_id) {
  desired_current_id_ = surface_id;
  has_desired_current_ = true;
}

std::uint32_t IOSApp::ModelCurrentId() const {
  auto const& current = ui_application_->surfaces->mobile_current;
  if (!current) {
    return 0;
  }
  return current->obj_id.id();
}

std::uint32_t IOSApp::EffectiveCurrentId() const {
  if (has_desired_current_) {
    for (auto const& surface : ui_application_->surfaces->surfaces) {
      if (surface->obj_id.id() == desired_current_id_) {
        return desired_current_id_;
      }
    }
  }
  return ModelCurrentId();
}

std::size_t IOSApp::IndexOfSurfaceId(std::uint32_t surface_id) const {
  auto const& surfaces = ui_application_->surfaces->surfaces;
  for (std::size_t i = 0; i < surfaces.size(); ++i) {
    if (surfaces[i]->obj_id.id() == surface_id) {
      return i;
    }
  }
  return 0;
}

void IOSApp::EnsureModelCurrentSeeded() {
  if (ui_application_->surfaces->mobile_current) {
    SetDesiredCurrent(ModelCurrentId());
    return;
  }
  auto const& surfaces = ui_application_->surfaces->surfaces;
  if (surfaces.empty()) {
    return;
  }
  SetDesiredCurrent(surfaces[0]->obj_id.id());
  IOSSurfacePresenter::ptr presenter{surfaces[0]->presenter};
  presenter->PageShown();
}

void IOSApp::ReconcileDesiredAfterTopologyChange(std::size_t previous_count) {
  auto const& surfaces = ui_application_->surfaces->surfaces;
  std::size_t const count = surfaces.size();
  if (count == 0) {
    has_desired_current_ = false;
    return;
  }
  // Add appends: settle on the new page.
  if (count > previous_count) {
    SetDesiredCurrent(surfaces[count - 1]->obj_id.id());
    return;
  }
  // Remove: keep desired if still live; else nearest neighbor by prior index.
  if (has_desired_current_) {
    for (auto const& surface : surfaces) {
      if (surface->obj_id.id() == desired_current_id_) {
        return;
      }
    }
  }
  std::size_t const neighbor =
      std::min(current_index_, count - 1);
  SetDesiredCurrent(surfaces[neighbor]->obj_id.id());
}

IOSSurfacePresenter::ptr IOSApp::FindLivePresenter(std::uint32_t surface_id) {
  for (auto const& surface : ui_application_->surfaces->surfaces) {
    if (surface->obj_id.id() == surface_id) {
      return IOSSurfacePresenter::ptr{surface->presenter};
    }
  }
  return {};
}

IOSSurfacePresenter::ptr IOSApp::CurrentPresenter() {
  std::uint32_t const id = EffectiveCurrentId();
  if (id != 0) {
    if (auto presenter = FindLivePresenter(id)) {
      return presenter;
    }
  }
  return IOSSurfacePresenter::ptr{
      ui_application_->surfaces->surfaces[current_index_]->presenter};
}

}  // namespace apptraverse
