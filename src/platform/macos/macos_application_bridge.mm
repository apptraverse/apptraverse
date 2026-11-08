#import "apptraverse/platform/macos/ATMacApplicationBridge.h"

#include <cstdint>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

#include "apptraverse/application_ui_session.h"
#include "apptraverse/model_object_proxy.h"
#include "apptraverse/platform/macos/macos_application_bridge.h"
#include "apptraverse/platform/macos/macos_ui_native_views.h"
#include "apptraverse/platform/macos/macos_ui_presenters.h"
#include "apptraverse/ui/ui_list_container.h"
#include "apptraverse/ui/ui_localization_string.h"

using apptraverse::ui::macos::MacDesktopWindowPresenter;
using apptraverse::ui::macos::MacListContainerPresenter;
using apptraverse::ui::macos::MacPushButtonPresenter;

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

class MacNativeViews final
    : public apptraverse::ui::macos::MacUiNativeViews {
 public:
  explicit MacNativeViews(ATMacApplicationBridge* owner) : owner_{owner} {}

  void LoadDesktopWindow(MacDesktopWindowPresenter& presenter) override {
    [owner_.delegate bridge:owner_ loadWindow:Token(presenter)];
  }
  void SyncDesktopWindow(MacDesktopWindowPresenter& presenter) override {
    [owner_.delegate bridge:owner_ syncWindow:Token(presenter)];
  }
  void UnloadDesktopWindow(MacDesktopWindowPresenter& presenter) override {
    [owner_.delegate bridge:owner_ unloadWindow:Token(presenter)];
  }
  void LoadPushButton(MacPushButtonPresenter& presenter) override {
    [owner_.delegate bridge:owner_ loadButton:Token(presenter)];
  }
  void SyncPushButton(MacPushButtonPresenter& presenter) override {
    [owner_.delegate bridge:owner_ syncButton:Token(presenter)];
  }
  void UnloadPushButton(MacPushButtonPresenter& presenter) override {
    [owner_.delegate bridge:owner_ unloadButton:Token(presenter)];
  }
  void LoadListContainer(MacListContainerPresenter& presenter) override {
    [owner_.delegate bridge:owner_ loadList:Token(presenter)];
  }
  void SyncListContainer(MacListContainerPresenter& presenter) override {
    [owner_.delegate bridge:owner_ syncList:Token(presenter)];
  }
  void UnloadListContainer(MacListContainerPresenter& presenter) override {
    [owner_.delegate bridge:owner_ unloadList:Token(presenter)];
  }

 private:
  __unsafe_unretained ATMacApplicationBridge* owner_;
};

}  // namespace

@interface ATMacWindowState ()
- (instancetype)initWithTitle:(NSString*)title
                            x:(double)x
                            y:(double)y
                        width:(double)width
                       height:(double)height;
@end

@implementation ATMacWindowState
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

@interface ATMacButtonState ()
- (instancetype)initWithParentToken:(uint64_t)parentToken
                              title:(NSString*)title
                            enabled:(BOOL)enabled
                                  x:(double)x
                                  y:(double)y
                              width:(double)width
                             height:(double)height;
@end

@implementation ATMacButtonState
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

@interface ATMacListRowState ()
- (instancetype)initWithIdentity:(uint64_t)identity text:(NSString*)text;
@end

@implementation ATMacListRowState
- (instancetype)initWithIdentity:(uint64_t)identity text:(NSString*)text {
  if ((self = [super init])) {
    _identity = identity;
    _text = text;
  }
  return self;
}
@end

@interface ATMacListState ()
- (instancetype)initWithParentToken:(uint64_t)parentToken
                               rows:(NSArray<ATMacListRowState*>*)rows
                        selectedRow:(NSInteger)selectedRow
                                  x:(double)x
                                  y:(double)y
                              width:(double)width
                             height:(double)height;
@end

@implementation ATMacListState
- (instancetype)initWithParentToken:(uint64_t)parentToken
                               rows:(NSArray<ATMacListRowState*>*)rows
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

@interface ATMacApplicationBridge () {
  apptraverse::ui::macos::MacApplicationBridgeCallbacks _callbacks;
  apptraverse::ApplicationModelRuntime _runtime;
  std::unique_ptr<apptraverse::ModelObjectProxy> _proxy;
  std::unique_ptr<MacNativeViews> _nativeViews;
  std::unique_ptr<apptraverse::ApplicationUiSession> _uiSession;
  std::thread _modelThread;
  int _modelExitStatus;
}
@property(nonatomic, weak, readwrite)
    id<ATMacApplicationBridgeDelegate> delegate;
- (instancetype)initWithCallbacks:
                    (apptraverse::ui::macos::MacApplicationBridgeCallbacks)
                        callbacks
                         delegate:(id<ATMacApplicationBridgeDelegate>)delegate;
@end

@implementation ATMacApplicationBridge

- (instancetype)initWithCallbacks:
                    (apptraverse::ui::macos::MacApplicationBridgeCallbacks)
                        callbacks
                         delegate:(id<ATMacApplicationBridgeDelegate>)delegate {
  if ((self = [super init])) {
    _callbacks = std::move(callbacks);
    _delegate = delegate;
    _modelExitStatus = 1;
  }
  return self;
}

- (void)start {
  _proxy = std::make_unique<apptraverse::ModelObjectProxy>(
      [self](apptraverse::ModelObjectProxy::ModelWork work) {
        _runtime.Post(std::move(work));
      });
  _nativeViews = std::make_unique<MacNativeViews>(self);
  _modelThread = std::thread([self] {
    _modelExitStatus = _callbacks.run_model(
        _runtime, [self](apptraverse::ApplicationPublicationKind kind) {
          dispatch_async(dispatch_get_main_queue(), ^{
            [self
                consumePublication:
                    kind == apptraverse::ApplicationPublicationKind::Initial
                        ? ATMacPublicationKindInitial
                        : ATMacPublicationKindIncremental];
          });
        });
    dispatch_async(dispatch_get_main_queue(), ^{
      [self.delegate bridgeModelDidFinish:self];
    });
  });
}

- (void)consumePublication:(ATMacPublicationKind)kind {
  std::vector<std::uint8_t> publication =
      _runtime.TakePublishedCopyForUiApply();
  if (kind == ATMacPublicationKindInitial) {
    _uiSession = std::make_unique<apptraverse::ApplicationUiSession>(
        publication, _nativeViews.get(), *_proxy);
  } else {
    _uiSession->ApplyStructural(publication);
  }
  _runtime.NotifyPublicationConsumed();
}

- (ATMacWindowState*)windowState:(uint64_t)token {
  auto& presenter = FromToken<MacDesktopWindowPresenter>(token);
  auto desktop = presenter.desktop_window();
  NSString* title = ToNSString(presenter.ResolvePresentedWindowTitle(
      presenter.locale->language_index()));
  return [[ATMacWindowState alloc]
      initWithTitle:title
                  x:desktop->x()
                  y:desktop->y()
              width:desktop->client_width()
             height:desktop->client_height()];
}

- (ATMacButtonState*)buttonState:(uint64_t)token {
  auto& presenter = FromToken<MacPushButtonPresenter>(token);
  auto layout = presenter.window_presenter->ResolveChildLayout(
      {presenter.layout_x, presenter.layout_y, presenter.layout_width,
       presenter.layout_height, presenter.control_id});
  NSString* title = ToNSString(presenter.button->label->Resolve(
      presenter.window_presenter->locale->language_index()));
  return [[ATMacButtonState alloc]
      initWithParentToken:Token(*presenter.window_presenter)
                    title:title
                  enabled:presenter.button->enabled()
                        x:layout.x
                        y:layout.y
                    width:layout.width
                   height:layout.height];
}

- (ATMacListState*)listState:(uint64_t)token {
  auto& presenter = FromToken<MacListContainerPresenter>(token);
  auto layout = presenter.window_presenter->ResolveChildLayout(
      {presenter.layout_x, presenter.layout_y, presenter.layout_width,
       presenter.layout_height, presenter.control_id});
  int const language_index =
      presenter.window_presenter->locale->language_index();
  NSMutableArray<ATMacListRowState*>* rows =
      [[NSMutableArray alloc] initWithCapacity:presenter.container->rows.size()];
  for (auto const& row : presenter.container->rows) {
    auto const identity = static_cast<std::uint64_t>(row.id().id());
    [rows addObject:[[ATMacListRowState alloc]
                        initWithIdentity:identity
                                   text:ToNSString(presenter.ResolveListRowPrimaryText(
                                            row, language_index))]];
  }
  return [[ATMacListState alloc]
      initWithParentToken:Token(*presenter.window_presenter)
                     rows:rows
              selectedRow:presenter.container->selection_index
                        x:layout.x
                        y:layout.y
                    width:layout.width
                   height:layout.height];
}

- (void)clickButton:(uint64_t)token {
  FromToken<MacPushButtonPresenter>(token).NotifyClickedFromUi();
}

- (void)selectListRow:(NSInteger)row token:(uint64_t)token {
  FromToken<MacListContainerPresenter>(token)
      .SubmitListRowSelectionChangedFromUi(static_cast<std::size_t>(row));
}

- (void)submitWindowX:(double)x
                    y:(double)y
                width:(double)width
               height:(double)height
                token:(uint64_t)token {
  FromToken<MacDesktopWindowPresenter>(token).SubmitFrameFromNative(
      static_cast<std::int32_t>(x), static_cast<std::int32_t>(y),
      static_cast<std::int32_t>(width), static_cast<std::int32_t>(height));
}

- (BOOL)requestCloseWindow:(uint64_t)token {
  auto& presenter = FromToken<MacDesktopWindowPresenter>(token);
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

namespace apptraverse::ui::macos {

ATMacApplicationBridge* MakeMacApplicationBridge(
    MacApplicationBridgeCallbacks callbacks,
    id<ATMacApplicationBridgeDelegate> delegate) {
  return [[ATMacApplicationBridge alloc] initWithCallbacks:std::move(callbacks)
                                                 delegate:delegate];
}

}  // namespace apptraverse::ui::macos
