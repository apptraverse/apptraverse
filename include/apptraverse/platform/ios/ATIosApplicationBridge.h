#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

typedef NS_ENUM(NSInteger, ATIosPublicationKind) {
  ATIosPublicationKindInitial,
  ATIosPublicationKindIncremental,
};

@class ATIosApplicationBridge;

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
- (void)bridgeModelDidFinish:(ATIosApplicationBridge*)bridge;
- (void)bridge:(ATIosApplicationBridge*)bridge
    backgroundSaveDidFinish:(NSInteger)result;
@end

@interface ATIosWindowState : NSObject
@property(nonatomic, readonly) uint64_t identity;
@property(nonatomic, readonly) NSString* title;
@property(nonatomic, readonly) double x;
@property(nonatomic, readonly) double y;
@property(nonatomic, readonly) double width;
@property(nonatomic, readonly) double height;
@end

@interface ATIosButtonState : NSObject
@property(nonatomic, readonly) uint64_t parentToken;
@property(nonatomic, readonly) NSString* title;
@property(nonatomic, readonly, getter=isEnabled) BOOL enabled;
@property(nonatomic, readonly) double x;
@property(nonatomic, readonly) double y;
@property(nonatomic, readonly) double width;
@property(nonatomic, readonly) double height;
@end

@interface ATIosListRowState : NSObject
@property(nonatomic, readonly) uint64_t identity;
@property(nonatomic, readonly) NSString* text;
@end

@interface ATIosListState : NSObject
@property(nonatomic, readonly) uint64_t parentToken;
@property(nonatomic, readonly) NSArray<ATIosListRowState*>* rows;
@property(nonatomic, readonly) NSInteger selectedRow;
@property(nonatomic, readonly) double x;
@property(nonatomic, readonly) double y;
@property(nonatomic, readonly) double width;
@property(nonatomic, readonly) double height;
@end

@interface ATIosApplicationBridge : NSObject
@property(nonatomic, weak, readonly)
    id<ATIosApplicationBridgeDelegate> delegate;
- (instancetype)init NS_UNAVAILABLE;
- (void)start;
- (void)consumePublication:(ATIosPublicationKind)kind;
- (ATIosWindowState*)windowState:(uint64_t)token;
- (ATIosButtonState*)buttonState:(uint64_t)token;
- (ATIosListState*)listState:(uint64_t)token;
- (void)clickButton:(uint64_t)token;
- (void)selectListRow:(NSInteger)row token:(uint64_t)token;
- (void)activateListRow:(NSInteger)row token:(uint64_t)token;
- (void)submitViewportWidth:(double)width height:(double)height;
- (void)submitMobileSwipe:(NSInteger)delta;
- (void)submitMobilePage:(uint64_t)token;
- (void)submitWindowX:(double)x
                    y:(double)y
                width:(double)width
               height:(double)height
                token:(uint64_t)token;
- (BOOL)requestCloseWindow:(uint64_t)token;
- (void)requestBackgroundSave;
- (void)requestStop;
- (NSInteger)finishTeardown;
@end

NS_ASSUME_NONNULL_END
