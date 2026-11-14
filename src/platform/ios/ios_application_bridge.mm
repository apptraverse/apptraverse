#import "apptraverse/platform/ios/ATIosApplicationBridge.h"

#include <cstdlib>
#include <cstdint>
#include <cmath>
#include <limits>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/object_serialization.h"

#include "apptraverse/application_ui_session.h"
#include "apptraverse/model_object_proxy.h"
#include "apptraverse/platform/ios/ios_application_bridge.h"
#include "apptraverse/platform/ios/ios_ui_native_views.h"
#include "apptraverse/platform/ios/ios_ui_presenters.h"
#include "apptraverse/ui/ui_list_container.h"
#include "apptraverse/ui/ui_localization_string.h"

using apptraverse::ui::ios::IosDesktopWindowPresenter;
using apptraverse::ui::ios::IosListContainerPresenter;
using apptraverse::ui::ios::IosPushButtonPresenter;

namespace {

template <typename T>
uint64_t Token(T const& value) {
  return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&value));
}

template <typename T>
T& FromToken(uint64_t token) {
  return *reinterpret_cast<T*>(static_cast<uintptr_t>(token));
}

NSString* ToNSString(std::string const& text) {
  NSString* result = [[NSString alloc] initWithBytes:text.data()
                                              length:text.size()
                                            encoding:NSUTF8StringEncoding];
  if (result == nil) {
    apptraverse::WriteFatalStderr("cannot create native text from UTF-8 publication\n");
    std::abort();
  }
  return result;
}

class IosNativeViews final
    : public apptraverse::ui::ios::IosUiNativeViews {
 public:
  explicit IosNativeViews(ATIosApplicationBridge* owner) : owner_{owner} {}

  void LoadDesktopWindow(IosDesktopWindowPresenter& presenter) override {
    [owner_.delegate bridge:owner_ loadWindow:Token(presenter)];
  }
  void SyncDesktopWindow(IosDesktopWindowPresenter& presenter) override {
    [owner_.delegate bridge:owner_ syncWindow:Token(presenter)];
  }
  void UnloadDesktopWindow(IosDesktopWindowPresenter& presenter) override {
    [owner_.delegate bridge:owner_ unloadWindow:Token(presenter)];
  }
  void LoadPushButton(IosPushButtonPresenter& presenter) override {
    [owner_.delegate bridge:owner_ loadButton:Token(presenter)];
  }
  void SyncPushButton(IosPushButtonPresenter& presenter) override {
    [owner_.delegate bridge:owner_ syncButton:Token(presenter)];
  }
  void UnloadPushButton(IosPushButtonPresenter& presenter) override {
    [owner_.delegate bridge:owner_ unloadButton:Token(presenter)];
  }
  void LoadListContainer(IosListContainerPresenter& presenter) override {
    [owner_.delegate bridge:owner_ loadList:Token(presenter)];
  }
  void SyncListContainer(IosListContainerPresenter& presenter) override {
    [owner_.delegate bridge:owner_ syncList:Token(presenter)];
  }
  void UnloadListContainer(IosListContainerPresenter& presenter) override {
    [owner_.delegate bridge:owner_ unloadList:Token(presenter)];
  }

 private:
  __unsafe_unretained ATIosApplicationBridge* owner_;
};

}  // namespace

@interface ATIosWindowState ()
- (instancetype)initWithTitle:(NSString*)title
                     identity:(uint64_t)identity
                            x:(double)x
                            y:(double)y
                        width:(double)width
                       height:(double)height;
@end

@implementation ATIosWindowState
- (instancetype)initWithTitle:(NSString*)title
                     identity:(uint64_t)identity
                            x:(double)x
                            y:(double)y
                        width:(double)width
                       height:(double)height {
  if ((self = [super init])) {
    _title = title;
    _identity = identity;
    _x = x;
    _y = y;
    _width = width;
    _height = height;
  }
  return self;
}
@end

@interface ATIosButtonState ()
- (instancetype)initWithParentToken:(uint64_t)parentToken
                              title:(NSString*)title
                            enabled:(BOOL)enabled
                                  x:(double)x
                                  y:(double)y
                              width:(double)width
                             height:(double)height;
@end

@implementation ATIosButtonState
- (instancetype)initWithParentToken:(uint64_t)parentToken
                              title:(NSString*)title
                            enabled:(BOOL)enabled
                                  x:(double)x
                                  y:(double)y
                              width:(double)width
                             height:(double)height {
  if ((self = [super init])) {
    _parentToken = parentToken;
    _title = title;
    _enabled = enabled;
    _x = x;
    _y = y;
    _width = width;
    _height = height;
  }
  return self;
}
@end

@interface ATIosListRowState ()
- (instancetype)initWithIdentity:(uint64_t)identity text:(NSString*)text;
@end

@implementation ATIosListRowState
- (instancetype)initWithIdentity:(uint64_t)identity text:(NSString*)text {
  if ((self = [super init])) {
    _identity = identity;
    _text = text;
  }
  return self;
}
@end

@interface ATIosListState ()
- (instancetype)initWithParentToken:(uint64_t)parentToken
                               rows:(NSArray<ATIosListRowState*>*)rows
                        selectedRow:(NSInteger)selectedRow
                                  x:(double)x
                                  y:(double)y
                              width:(double)width
                             height:(double)height;
@end

@implementation ATIosListState
- (instancetype)initWithParentToken:(uint64_t)parentToken
                               rows:(NSArray<ATIosListRowState*>*)rows
                        selectedRow:(NSInteger)selectedRow
                                  x:(double)x
                                  y:(double)y
                              width:(double)width
                             height:(double)height {
  if ((self = [super init])) {
    _parentToken = parentToken;
    _rows = rows;
    _selectedRow = selectedRow;
    _x = x;
    _y = y;
    _width = width;
    _height = height;
  }
  return self;
}
@end

@interface ATIosApplicationBridge () {
  apptraverse::ui::ios::IosApplicationBridgeCallbacks _callbacks;
  apptraverse::ApplicationModelRuntime _ownedRuntime;
  apptraverse::ApplicationModelRuntime* _runtime;
  std::unique_ptr<apptraverse::ModelObjectProxy> _proxy;
  std::unique_ptr<IosNativeViews> _nativeViews;
  std::unique_ptr<apptraverse::ApplicationUiSession> _uiSession;
  std::thread _modelThread;
  int _modelExitStatus;
  bool _stopping;
  std::int32_t _viewportWidth;
  std::int32_t _viewportHeight;
}
@property(nonatomic, weak, readwrite)
    id<ATIosApplicationBridgeDelegate> delegate;
- (instancetype)initWithCallbacks:
                    (apptraverse::ui::ios::IosApplicationBridgeCallbacks)
                        callbacks
                         delegate:(id<ATIosApplicationBridgeDelegate>)delegate;
- (void)publishMobilePresentation;
@end

@implementation ATIosApplicationBridge

- (instancetype)initWithCallbacks:
                    (apptraverse::ui::ios::IosApplicationBridgeCallbacks)
                        callbacks
                         delegate:(id<ATIosApplicationBridgeDelegate>)delegate {
  if ((self = [super init])) {
    _callbacks = std::move(callbacks);
    _delegate = delegate;
    _modelExitStatus = 1;
    _runtime = _callbacks.runtime ? _callbacks.runtime : &_ownedRuntime;
    _stopping = false;
    _viewportWidth = 0;
    _viewportHeight = 0;
  }
  return self;
}

- (void)start {
  _proxy = std::make_unique<apptraverse::ModelObjectProxy>(
      [self](apptraverse::ModelObjectProxy::ModelWork work) {
        _runtime->Post(std::move(work));
      });
  _nativeViews = std::make_unique<IosNativeViews>(self);
  _modelThread = std::thread([self] {
    _modelExitStatus = _callbacks.run_model(
        *_runtime, [self](apptraverse::ApplicationPublicationKind kind) {
          dispatch_async(dispatch_get_main_queue(), ^{
            [self
                consumePublication:
                    kind == apptraverse::ApplicationPublicationKind::Initial
                        ? ATIosPublicationKindInitial
                        : ATIosPublicationKindIncremental];
          });
        });
    dispatch_async(dispatch_get_main_queue(), ^{
      id<ATIosApplicationBridgeDelegate> delegate = self.delegate;
      if (delegate != nil) {
        [delegate bridgeModelDidFinish:self];
      } else {
        // The Swift host can be released before the model finishes. Break the
        // model/proxy lifetime only after the worker has actually returned.
        [self finishTeardown];
      }
    });
  });
}

- (void)requestBackgroundSave {
  if (_stopping) return;
  // Post also accepts work before Run's initial publication. The callback runs
  // only after application preparation, behind previously accepted commands,
  // on the same model thread and Domain as the continuing session.
  _runtime->Post([self](ae::Domain& domain) {
    int const result = _callbacks.persist_model(domain);
    dispatch_async(dispatch_get_main_queue(), ^{
      [self.delegate bridge:self backgroundSaveDidFinish:result];
    });
  });
}

- (void)publishMobilePresentation {
  auto const snapshot = _callbacks.mobile_presentation(_uiSession->root());
  std::vector<ae::Obj*> objects;
  apptraverse::CollectLiveReachableObjects(_uiSession->root(), objects);
  auto& registry = ae::Registry::GetRegistry();
  auto token_for = [&](ae::ObjId id) -> uint64_t {
    for (auto* object : objects) {
      if (registry.GenerationDistance(IosDesktopWindowPresenter::kClassId,
                                      object->GetClassId()) < 0) continue;
      auto& presenter = *static_cast<IosDesktopWindowPresenter*>(object);
      if (presenter.presentation_loaded && presenter.window.id() == id) {
        return Token(presenter);
      }
    }
    apptraverse::WriteFatalStderr("fatal: iOS mobile page has no live presenter\n");
    std::abort();
  };
  NSMutableArray<NSNumber*>* pages = [NSMutableArray arrayWithCapacity:snapshot.windows.size()];
  for (auto id : snapshot.windows) [pages addObject:@(token_for(id))];
  [self.delegate bridge:self applyMobilePages:pages
              activeToken:token_for(snapshot.active_window)];
  if (_viewportWidth > 0 && _viewportHeight > 0 && !_stopping &&
      _callbacks.on_viewport) {
    _callbacks.on_viewport(_uiSession->root(), *_proxy,
                           _viewportWidth, _viewportHeight);
  }
}

- (void)consumePublication:(ATIosPublicationKind)kind {
  if (self.delegate == nil) {
    // Host destruction is a teardown state, not a partially loaded UI. Drain
    // the publication slot so the stopped model can finish without a host.
    [self requestStop];
    _runtime->AbandonUnconsumedPublicationsForTeardown();
    return;
  }
  std::vector<std::uint8_t> publication =
      _runtime->TakePublishedCopyForUiApply(
          kind == ATIosPublicationKindInitial
              ? apptraverse::TakePublishedSidecarCallback{}
              : _callbacks.take_incremental_sidecar);
  if (kind == ATIosPublicationKindInitial) {
    _uiSession = std::make_unique<apptraverse::ApplicationUiSession>(
        publication, _nativeViews.get(), *_proxy,
        _callbacks.prepare_ui_presenters);
    if (_callbacks.after_initial) _callbacks.after_initial(_uiSession->root());
  } else {
    _uiSession->ApplyStructural(publication);
    if (_callbacks.after_incremental) _callbacks.after_incremental(_uiSession->root());
  }
  [self publishMobilePresentation];
  _runtime->NotifyPublicationConsumed();
}

- (ATIosWindowState*)windowState:(uint64_t)token {
  auto& presenter = FromToken<IosDesktopWindowPresenter>(token);
  auto desktop = presenter.desktop_window();
  NSString* title = ToNSString(presenter.ResolvePresentedWindowTitle(
      presenter.locale->language_index()));
  return [[ATIosWindowState alloc]
      initWithTitle:title
           identity:desktop->obj_id.id()
                  x:desktop->x()
                  y:desktop->y()
              width:desktop->client_width()
             height:desktop->client_height()];
}

- (ATIosButtonState*)buttonState:(uint64_t)token {
  auto& presenter = FromToken<IosPushButtonPresenter>(token);
  auto layout = presenter.window_presenter->ResolveChildLayout(
      {presenter.layout_x, presenter.layout_y, presenter.layout_width,
       presenter.layout_height, presenter.control_id});
  NSString* title = ToNSString(presenter.button->label->Resolve(
      presenter.window_presenter->locale->language_index()));
  return [[ATIosButtonState alloc]
      initWithParentToken:Token(*presenter.window_presenter)
                    title:title
                  enabled:presenter.button->enabled()
                        x:layout.x
                        y:layout.y
                    width:layout.width
                   height:layout.height];
}

- (ATIosListState*)listState:(uint64_t)token {
  auto& presenter = FromToken<IosListContainerPresenter>(token);
  auto layout = presenter.window_presenter->ResolveChildLayout(
      {presenter.layout_x, presenter.layout_y, presenter.layout_width,
       presenter.layout_height, presenter.control_id});
  int const language_index =
      presenter.window_presenter->locale->language_index();
  NSMutableArray<ATIosListRowState*>* rows =
      [[NSMutableArray alloc] initWithCapacity:presenter.container->rows.size()];
  for (auto const& row : presenter.container->rows) {
    auto const identity = static_cast<std::uint64_t>(row.id().id());
    [rows addObject:[[ATIosListRowState alloc]
                        initWithIdentity:identity
                                   text:ToNSString(
                                            presenter.ResolveListRowPrimaryText(
                                                    row, language_index))]];
  }
  return [[ATIosListState alloc]
      initWithParentToken:Token(*presenter.window_presenter)
                     rows:rows
              selectedRow:presenter.container->selection_index
                        x:layout.x
                        y:layout.y
                    width:layout.width
                   height:layout.height];
}

- (void)clickButton:(uint64_t)token {
  if (_stopping) return;
  FromToken<IosPushButtonPresenter>(token).NotifyClickedFromUi();
}

- (void)selectListRow:(NSInteger)row token:(uint64_t)token {
  if (_stopping) return;
  FromToken<IosListContainerPresenter>(token)
      .SubmitListRowSelectionChangedFromUi(static_cast<std::size_t>(row));
}

- (void)activateListRow:(NSInteger)row token:(uint64_t)token {
  if (_stopping) return;
  FromToken<IosListContainerPresenter>(token)
      .SubmitListRowActivatedFromUi(static_cast<std::size_t>(row));
}

- (void)submitViewportWidth:(double)width height:(double)height {
  if (_stopping) return;
  if (!std::isfinite(width) || !std::isfinite(height) ||
      width > std::numeric_limits<std::int32_t>::max() ||
      height > std::numeric_limits<std::int32_t>::max()) {
    apptraverse::WriteFatalStderr("invalid native iOS viewport\n");
    std::abort();
  }
  if (width < 1 || height < 1) return;
  _viewportWidth = static_cast<std::int32_t>(width);
  _viewportHeight = static_cast<std::int32_t>(height);
  if (!_uiSession) return;
  if (_callbacks.on_viewport) {
    _callbacks.on_viewport(_uiSession->root(), *_proxy, _viewportWidth, _viewportHeight);
  }
}

- (void)submitMobileSwipe:(NSInteger)delta {
  if (_stopping || !_uiSession || !_callbacks.on_mobile_swipe) return;
  _callbacks.on_mobile_swipe(_uiSession->root(), *_proxy, static_cast<int>(delta));
}

- (void)submitMobilePage:(uint64_t)token {
  if (_stopping || !_uiSession || !_callbacks.on_mobile_page) return;
  auto& presenter = FromToken<IosDesktopWindowPresenter>(token);
  _callbacks.on_mobile_page(_uiSession->root(), *_proxy, presenter.window.id());
}

- (void)submitWindowX:(double)x y:(double)y width:(double)width
               height:(double)height token:(uint64_t)token {
  (void)x; (void)y; (void)token;
  [self submitViewportWidth:width height:height];
}

- (BOOL)requestCloseWindow:(uint64_t)token {
  if (_stopping) return YES;
  auto& presenter = FromToken<IosDesktopWindowPresenter>(token);
  bool const stops_application = _callbacks.close_desktop_window(
      _uiSession->root(), *presenter.window, *_proxy,
      [self] { _runtime->RequestStop(); });
  _stopping = stops_application;
  return stops_application;
}

- (void)requestStop {
  if (_stopping) return;
  _stopping = true;
  _runtime->RequestStop();
}

- (NSInteger)finishTeardown {
  _modelThread.join();
  _uiSession.reset();
  _nativeViews.reset();
  _proxy.reset();
  self.delegate = nil;
  return _modelExitStatus;
}

@end

namespace apptraverse::ui::ios {

ATIosApplicationBridge* MakeIosApplicationBridge(
    IosApplicationBridgeCallbacks callbacks,
    id<ATIosApplicationBridgeDelegate> delegate) {
  return [[ATIosApplicationBridge alloc] initWithCallbacks:std::move(callbacks)
                                                 delegate:delegate];
}

}  // namespace apptraverse::ui::ios
