#import "apptraverse/platform/ios/ATIosApplicationBridge.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

#include "apptraverse/noninteractive_crt.h"

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
  return [[NSString alloc] initWithBytes:text.data()
                                  length:text.size()
                                encoding:NSUTF8StringEncoding];
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
                            x:(double)x
                            y:(double)y
                        width:(double)width
                       height:(double)height;
@end

@implementation ATIosWindowState
- (instancetype)initWithTitle:(NSString*)title
                            x:(double)x
                            y:(double)y
                        width:(double)width
                       height:(double)height {
  if ((self = [super init])) {
    _title = title;
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
  apptraverse::ApplicationModelRuntime _runtime;
  std::unique_ptr<apptraverse::ModelObjectProxy> _proxy;
  std::unique_ptr<IosNativeViews> _nativeViews;
  std::unique_ptr<apptraverse::ApplicationUiSession> _uiSession;
  std::thread _modelThread;
  int _modelExitStatus;
  std::atomic<bool> _pending_background_save;
  std::atomic<bool> _background_save_running;
  std::atomic<bool> _model_thread_live;
}
@property(nonatomic, weak, readwrite)
    id<ATIosApplicationBridgeDelegate> delegate;
- (instancetype)initWithCallbacks:
                    (apptraverse::ui::ios::IosApplicationBridgeCallbacks)
                        callbacks
                         delegate:(id<ATIosApplicationBridgeDelegate>)delegate;
- (void)enqueueBackgroundSaveWork;
- (void)tryStartBackgroundSaveFromPending;
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
    _pending_background_save.store(false);
    _background_save_running.store(false);
    _model_thread_live.store(false);
  }
  return self;
}

- (void)start {
  _proxy = std::make_unique<apptraverse::ModelObjectProxy>(
      [self](apptraverse::ModelObjectProxy::ModelWork work) {
        _runtime.Post(std::move(work));
      });
  _nativeViews = std::make_unique<IosNativeViews>(self);
  _modelThread = std::thread([self] {
    _model_thread_live.store(true, std::memory_order_release);
    _modelExitStatus = _callbacks.run_model(
        _runtime, [self](apptraverse::ApplicationPublicationKind kind) {
          dispatch_async(dispatch_get_main_queue(), ^{
            [self
                consumePublication:
                    kind == apptraverse::ApplicationPublicationKind::Initial
                        ? ATIosPublicationKindInitial
                        : ATIosPublicationKindIncremental];
          });
        });
    _model_thread_live.store(false, std::memory_order_release);
    dispatch_async(dispatch_get_main_queue(), ^{
      [self.delegate bridgeModelDidFinish:self];
    });
  });
}

- (void)enqueueBackgroundSaveWork {
  _runtime.Post([self](ae::Domain& domain) {
    int const persist_result = _callbacks.persist_model
                                   ? _callbacks.persist_model(domain)
                                   : 1;
    dispatch_async(dispatch_get_main_queue(), ^{
      [self.delegate bridge:self backgroundSaveDidFinish:persist_result];
      self->_background_save_running.store(false, std::memory_order_release);
      if (self->_pending_background_save.load(std::memory_order_acquire)) {
        [self tryStartBackgroundSaveFromPending];
      }
    });
  });
}

- (void)tryStartBackgroundSaveFromPending {
  if (!_callbacks.persist_model) {
    return;
  }
  if (!_pending_background_save.load(std::memory_order_acquire)) {
    return;
  }
  bool expected = false;
  if (!_background_save_running.compare_exchange_strong(
          expected, true, std::memory_order_acq_rel)) {
    return;
  }
  _pending_background_save.store(false, std::memory_order_release);
  [self enqueueBackgroundSaveWork];
}

- (void)requestBackgroundSave {
  if (!_callbacks.persist_model) {
    apptraverse::WriteFatalStderr(
        "fatal: ATIosApplicationBridge::requestBackgroundSave without "
        "persist_model\n");
    std::abort();
  }
  _pending_background_save.store(true, std::memory_order_release);
  if (!_model_thread_live.load(std::memory_order_acquire)) {
    return;
  }
  dispatch_async(dispatch_get_main_queue(), ^{
    [self tryStartBackgroundSaveFromPending];
  });
}

- (void)consumePublication:(ATIosPublicationKind)kind {
  std::vector<std::uint8_t> publication =
      _runtime.TakePublishedCopyForUiApply();
  if (kind == ATIosPublicationKindInitial) {
    _uiSession = std::make_unique<apptraverse::ApplicationUiSession>(
        publication, _nativeViews.get(), *_proxy);
  } else {
    _uiSession->ApplyStructural(publication);
  }
  _runtime.NotifyPublicationConsumed();
}

- (ATIosWindowState*)windowState:(uint64_t)token {
  auto& presenter = FromToken<IosDesktopWindowPresenter>(token);
  auto desktop = presenter.desktop_window();
  NSString* title = ToNSString(presenter.ResolvePresentedWindowTitle(
      presenter.locale->language_index()));
  return [[ATIosWindowState alloc]
      initWithTitle:title
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
                                            apptraverse::ui::
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
  FromToken<IosPushButtonPresenter>(token).NotifyClickedFromUi();
}

- (void)selectListRow:(NSInteger)row token:(uint64_t)token {
  FromToken<IosListContainerPresenter>(token)
      .SubmitListRowSelectionChangedFromUi(static_cast<std::size_t>(row));
}

- (void)submitWindowX:(double)x
                    y:(double)y
                width:(double)width
               height:(double)height
                token:(uint64_t)token {
  (void)x;
  (void)y;
  (void)width;
  (void)height;
  (void)token;
}

- (BOOL)requestCloseWindow:(uint64_t)token {
  auto& presenter = FromToken<IosDesktopWindowPresenter>(token);
  return _callbacks.close_desktop_window(
      _uiSession->root(), *presenter.window, *_proxy,
      [self] { _runtime.RequestStop(); });
}

- (void)requestStop {
  _runtime.RequestStop();
}

- (void)finishTeardown {
  _modelThread.join();
  _uiSession.reset();
  _nativeViews.reset();
  _proxy.reset();
  self.delegate = nil;
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
