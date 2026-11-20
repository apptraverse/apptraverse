#import <Foundation/Foundation.h>
#include <stdbool.h>
#import "../apple/ATAppleWindowLayout.h"

NS_ASSUME_NONNULL_BEGIN

typedef NS_ENUM(NSInteger, ATIosPublicationKind) {
  ATIosPublicationKindInitial,
  ATIosPublicationKindIncremental,
};

@class ATIosApplicationBridge;

typedef struct {
  double displayScale;
  double bodyTextScale;
  bool dark;
  bool highContrast;
  bool reducedMotion;
  bool voiceOver;
} ATIosDeviceSettings;

// Published menu values only. Native menu objects and handlers live in Swift.
@interface ATIosMenuItemState : NSObject
@property(nonatomic, readonly) NSString* title;
@property(nonatomic, readonly, getter=isSelected) BOOL selected;
- (instancetype)initWithTitle:(NSString*)title selected:(BOOL)selected;
@end

@interface ATIosMenuGroupState : NSObject
@property(nonatomic, readonly) NSString* title;
@property(nonatomic, readonly) NSArray<ATIosMenuItemState*>* items;
- (instancetype)initWithTitle:(NSString*)title
                        items:(NSArray<ATIosMenuItemState*>*)items;
@end

@interface ATIosSettingsState : NSObject
@property(nonatomic, readonly) NSString* title;
@property(nonatomic, readonly) NSString* doneTitle;
@property(nonatomic, readonly) NSArray<ATIosMenuGroupState*>* groups;
@property(nonatomic, readonly, getter=isPresented) BOOL presented;
- (instancetype)initWithTitle:(NSString*)title doneTitle:(NSString*)doneTitle
                       groups:(NSArray<ATIosMenuGroupState*>*)groups
                    presented:(BOOL)presented;
@end

@protocol ATIosApplicationBridgeDelegate <NSObject>
- (void)bridge:(ATIosApplicationBridge*)bridge
    loadWindow:(uint64_t)token;
- (void)bridge:(ATIosApplicationBridge*)bridge
    syncWindow:(uint64_t)token;
- (void)bridge:(ATIosApplicationBridge*)bridge
    unloadWindow:(uint64_t)token;
- (void)bridge:(ATIosApplicationBridge*)bridge
    loadButton:(uint64_t)token;
- (void)bridge:(ATIosApplicationBridge*)bridge
    syncButton:(uint64_t)token;
- (void)bridge:(ATIosApplicationBridge*)bridge
    unloadButton:(uint64_t)token;
- (void)bridge:(ATIosApplicationBridge*)bridge
    loadList:(uint64_t)token;
- (void)bridge:(ATIosApplicationBridge*)bridge
    syncList:(uint64_t)token;
- (void)bridge:(ATIosApplicationBridge*)bridge
    unloadList:(uint64_t)token;
- (void)bridge:(ATIosApplicationBridge*)bridge
    applyMobilePages:(NSArray<NSNumber*>*)tokens activeToken:(uint64_t)token;
- (void)bridgeDidFinishPublication:(ATIosApplicationBridge*)bridge;
- (void)bridgeModelDidFinish:(ATIosApplicationBridge*)bridge;
@optional
- (void)bridge:(ATIosApplicationBridge*)bridge
    syncSettings:(ATIosSettingsState*)settings;
@required
- (void)bridge:(ATIosApplicationBridge*)bridge
    backgroundSaveDidFinish:(NSInteger)result;
@end

@interface ATIosWindowState : NSObject
@property(nonatomic, readonly) uint64_t identity;
@property(nonatomic, readonly) NSString* title;
@property(nonatomic, readonly) ATAppleWindowLayout* layout;
@property(nonatomic, readonly) NSUInteger theme;
@property(nonatomic, readonly) BOOL softenDark;
@end

@interface ATIosButtonState : NSObject
@property(nonatomic, readonly) uint64_t parentToken;
@property(nonatomic, readonly) NSString* title;
@property(nonatomic, readonly, getter=isEnabled) BOOL enabled;
@property(nonatomic, readonly) uint32_t controlID;
@end

@interface ATIosListRowState : NSObject
@property(nonatomic, readonly) uint64_t identity;
@property(nonatomic, readonly) NSString* text;
@end

@interface ATIosListState : NSObject
@property(nonatomic, readonly) uint64_t parentToken;
@property(nonatomic, readonly) NSArray<ATIosListRowState*>* rows;
@property(nonatomic, readonly) NSInteger selectedRow;
@property(nonatomic, readonly) uint32_t controlID;
@end

@interface ATIosApplicationBridge : NSObject
@property(nonatomic, weak, readonly)
    id<ATIosApplicationBridgeDelegate> delegate;
- (instancetype)init NS_UNAVAILABLE;
- (void)observeDeviceSettings:(ATIosDeviceSettings)settings
    NS_SWIFT_NAME(observeDeviceSettings(_:));
- (void)start;
- (void)consumePublication:(ATIosPublicationKind)kind;
- (ATIosWindowState*)windowState:(uint64_t)token;
- (ATIosButtonState*)buttonState:(uint64_t)token;
- (ATIosListState*)listState:(uint64_t)token;
- (void)clickButton:(uint64_t)token;
- (void)executeMenuGroup:(NSUInteger)group item:(NSUInteger)item;
- (void)submitSettingsPresented:(BOOL)presented;
- (void)selectListRow:(NSInteger)row token:(uint64_t)token;
- (void)activateListRow:(NSInteger)row token:(uint64_t)token;
- (void)submitViewportWidth:(double)width height:(double)height;
- (void)submitMobileSwipe:(NSInteger)delta;
- (void)submitMobilePage:(uint64_t)token;
- (BOOL)requestCloseWindow:(uint64_t)token;
// Queue application persistence on the live model thread without teardown.
// Completion is delivered on the main queue through backgroundSaveDidFinish.
- (void)requestBackgroundSave;
- (void)requestStop;
- (NSInteger)finishTeardown;
@end

NS_ASSUME_NONNULL_END
