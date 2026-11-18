#import <Foundation/Foundation.h>

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
- (instancetype)initWithTitle:(NSString*)title selected:(BOOL)selected;
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
- (void)bridgeModelDidFinish:(ATMacApplicationBridge*)bridge;
@optional
- (void)bridge:(ATMacApplicationBridge*)bridge
    syncMenu:(NSArray<ATMacMenuGroupState*>*)groups;
@required
@end

@interface ATMacWindowState : NSObject
@property(nonatomic, readonly) NSString* title;
@property(nonatomic, readonly, getter=isPrimary) BOOL primary;
@property(nonatomic, readonly) double width;
@property(nonatomic, readonly) double height;
@end

@interface ATMacButtonState : NSObject
@property(nonatomic, readonly) uint64_t parentToken;
@property(nonatomic, readonly) NSString* title;
@property(nonatomic, readonly, getter=isEnabled) BOOL enabled;
@property(nonatomic, readonly) double x;
@property(nonatomic, readonly) double y;
@property(nonatomic, readonly) double width;
@property(nonatomic, readonly) double height;
@end

@interface ATMacListRowState : NSObject
@property(nonatomic, readonly) uint64_t identity;
@property(nonatomic, readonly) NSString* text;
@end

@interface ATMacListState : NSObject
@property(nonatomic, readonly) uint64_t parentToken;
@property(nonatomic, readonly) NSArray<ATMacListRowState*>* rows;
@property(nonatomic, readonly) NSInteger selectedRow;
@property(nonatomic, readonly) double x;
@property(nonatomic, readonly) double y;
@property(nonatomic, readonly) double width;
@property(nonatomic, readonly) double height;
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
- (void)selectListRow:(NSInteger)row token:(uint64_t)token;
- (void)activateListRow:(NSInteger)row token:(uint64_t)token;
- (void)submitWindowWidth:(double)width height:(double)height
                    token:(uint64_t)token;
- (void)activateWindow:(uint64_t)token;
- (BOOL)requestCloseWindow:(uint64_t)token;
- (void)requestStop;
- (NSInteger)finishTeardown;
@end

NS_ASSUME_NONNULL_END
