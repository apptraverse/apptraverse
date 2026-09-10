#import <UIKit/UIKit.h>

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
  RelayoutPages();

  SurfacesRootViewController* controller =
      (__bridge SurfacesRootViewController*)root_controller_;
  controller.loading.hidden = YES;
  controller.pager.hidden = NO;
  controller.addButton.hidden = NO;
  controller.removeButton.hidden = NO;
}

void IOSApp::OnIncrementalPublished() {
  std::vector<std::uint8_t> bytes;
  {
    std::lock_guard<std::mutex> lock{session_.mu};
    bytes = session_.channel.TakePublishedCopy();
  }
  session_.cv.notify_all();
  ApplySurfacesStructural(bytes, *ui_application_, ui_storage_, this,
                          &*model_proxy_);
  RelayoutPages();
}

void IOSApp::AddCurrentClick() { CurrentPresenter()->AddClick(); }

void IOSApp::RemoveCurrentClick() { CurrentPresenter()->RemoveClick(); }

void IOSApp::NoteVisiblePage(std::size_t index) { current_index_ = index; }

void IOSApp::LayoutPages() {
  // Pager geometry is laid out before the first publication arrives.
  if (ui_application_) {
    RelayoutPages();
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
    page.frame = CGRectMake(page_size.width * i, 0, page_size.width,
                            page_size.height);
  }
  pager.contentSize = CGSizeMake(page_size.width * count, page_size.height);

  // Add appends, so show the page that just appeared. Remove keeps the index,
  // so the nearest surviving page becomes current.
  if (count > page_count_) {
    current_index_ = count - 1;
  } else if (current_index_ >= count) {
    current_index_ = count - 1;
  }
  page_count_ = count;
  pager.contentOffset = CGPointMake(page_size.width * current_index_, 0);

  controller.removeButton.enabled =
      CurrentPresenter()->RemovableFromPager() ? YES : NO;
}

IOSSurfacePresenter::ptr IOSApp::CurrentPresenter() {
  return IOSSurfacePresenter::ptr{
      ui_application_->surfaces->surfaces[current_index_]->presenter};
}

}  // namespace apptraverse
