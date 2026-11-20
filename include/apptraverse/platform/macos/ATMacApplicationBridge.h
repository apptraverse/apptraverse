#import <Foundation/Foundation.h>
#import "../apple/ATAppleWindowLayout.h"

NS_ASSUME_NONNULL_BEGIN

typedef NS_ENUM(NSInteger, ATMacPublicationKind) {
  ATMacPublicationKindInitial,
  ATMacPublicationKindIncremental,
};

@class ATMacApplicationBridge;

// Published menu values only. Native menu objects and handlers live in Swift.
@interface ATMacMenuItemState : NSObject
@property(nonatomic, readonly) NSString* title;
@property(nonatomic, readonly, getter=isSelected) BOOL selected;
@property(nonatomic, readonly) BOOL startsSection;
- (instancetype)initWithTitle:(NSString*)title selected:(BOOL)selected;
- (instancetype)initWithTitle:(NSString*)title selected:(BOOL)selected
                startsSection:(BOOL)startsSection;
@end

@interface ATMacMenuGroupState : NSObject
@property(nonatomic, readonly) NSString* title;
@property(nonatomic, readonly) NSArray<ATMacMenuItemState*>* items;
- (instancetype)initWithTitle:(NSString*)title
                        items:(NSArray<ATMacMenuItemState*>*)items;
@end

@protocol ATMacApplicationBridgeDelegate <NSObject>
- (void)bridge:(ATMacApplicationBridge*)bridge
    loadWindow:(uint64_t)token;
- (void)bridge:(ATMacApplicationBridge*)bridge
    syncWindow:(uint64_t)token;
- (void)bridge:(ATMacApplicationBridge*)bridge
    unloadWindow:(uint64_t)token;
- (void)bridge:(ATMacApplicationBridge*)bridge
    loadButton:(uint64_t)token;
- (void)bridge:(ATMacApplicationBridge*)bridge
    syncButton:(uint64_t)token;
- (void)bridge:(ATMacApplicationBridge*)bridge
    unloadButton:(uint64_t)token;
- (void)bridge:(ATMacApplicationBridge*)bridge
    loadList:(uint64_t)token;
- (void)bridge:(ATMacApplicationBridge*)bridge
    syncList:(uint64_t)token;
- (void)bridge:(ATMacApplicationBridge*)bridge
    unloadList:(uint64_t)token;
- (void)bridge:(ATMacApplicationBridge*)bridge
    applyWindowFrame:(uint64_t)token;
- (void)bridge:(ATMacApplicationBridge*)bridge
    orderWindowFront:(uint64_t)token activate:(BOOL)activate;
- (NSArray<NSNumber*>*)orderedWindowTokens;
- (void)bridgeDidFinishPublication:(ATMacApplicationBridge*)bridge;
- (void)bridgeModelDidFinish:(ATMacApplicationBridge*)bridge;
@optional
- (void)bridge:(ATMacApplicationBridge*)bridge
    syncMenu:(NSArray<ATMacMenuGroupState*>*)groups;
- (void)bridge:(ATMacApplicationBridge*)bridge
    syncContextMenu:(NSArray<ATMacMenuItemState*>*)items;
@required
@end

@interface ATMacWindowDisplayState : NSObject
@property(nonatomic, readonly, getter=isFullscreen) BOOL fullscreen;
@property(nonatomic, readonly) NSString* actionTitle;
@property(nonatomic, readonly) ATAppleWindowLayout* toolbarLayout;
@property(nonatomic, readonly) uint32_t actionControlID;
- (instancetype)initWithFullscreen:(BOOL)fullscreen actionTitle:(NSString*)actionTitle
                    toolbarLayout:(ATAppleWindowLayout*)toolbarLayout
                   actionControlID:(uint32_t)actionControlID;
@end

@interface ATMacWindowState : NSObject
@property(nonatomic, readonly) NSString* title;
@property(nonatomic, readonly, getter=isPrimary) BOOL primary;
@property(nonatomic, readonly) ATAppleWindowLayout* layout;
@property(nonatomic, readonly) NSUInteger theme;
@property(nonatomic, readonly) BOOL softenDark;
@property(nonatomic, readonly) double width;
@property(nonatomic, readonly) double height;
@property(nonatomic, readonly, nullable) ATMacWindowDisplayState* display;
@end

@interface ATMacButtonState : NSObject
@property(nonatomic, readonly) uint64_t parentToken;
@property(nonatomic, readonly) NSString* title;
@property(nonatomic, readonly, getter=isEnabled) BOOL enabled;
@property(nonatomic, readonly) uint32_t controlID;
@end

@interface ATMacListRowState : NSObject
@property(nonatomic, readonly) uint64_t identity;
@property(nonatomic, readonly) NSString* text;
@end

@interface ATMacListState : NSObject
@property(nonatomic, readonly) uint64_t parentToken;
@property(nonatomic, readonly) NSArray<ATMacListRowState*>* rows;
@property(nonatomic, readonly) NSInteger selectedRow;
@property(nonatomic, readonly) uint32_t controlID;
@end

@interface ATMacApplicationBridge : NSObject
@property(nonatomic, weak, readonly)
    id<ATMacApplicationBridgeDelegate> delegate;
- (instancetype)init NS_UNAVAILABLE;
- (void)start;
- (void)consumePublication:(ATMacPublicationKind)kind;
- (ATMacWindowState*)windowState:(uint64_t)token;
- (ATMacButtonState*)buttonState:(uint64_t)token;
- (ATMacListState*)listState:(uint64_t)token;
- (void)clickButton:(uint64_t)token;
- (void)executeMenuGroup:(NSUInteger)group item:(NSUInteger)item;
- (void)executeContextMenuItem:(NSUInteger)item;
- (void)submitWindowFullscreen:(BOOL)fullscreen token:(uint64_t)token;
- (void)selectListRow:(NSInteger)row token:(uint64_t)token;
- (void)activateListRow:(NSInteger)row token:(uint64_t)token;
- (void)bindNativeWindow:(id)window token:(uint64_t)token;
- (void)unbindNativeWindow:(uint64_t)token;
- (void)applyWindowFullscreen:(BOOL)fullscreen token:(uint64_t)token;
- (void)observeNativeWindow:(uint64_t)token commit:(BOOL)commit;
- (void)beginNativeWindowMove:(uint64_t)token;
- (void)finishNativeWindowInteraction:(uint64_t)token commit:(BOOL)commit;
- (void)applyRequestedNativeFrame:(uint64_t)token;
- (void)observeSystemEnvironment;
- (void)activateWindow:(uint64_t)token;
- (BOOL)requestCloseWindow:(uint64_t)token;
- (void)requestStop;
- (NSInteger)finishTeardown;
@end

NS_ASSUME_NONNULL_END
