#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

typedef NS_ENUM(NSInteger, ATMinimalPublicationKind) {
  ATMinimalPublicationKindInitial,
  ATMinimalPublicationKindIncremental,
};

@class ATMinimalNativeBridge;

@protocol ATMinimalNativeBridgeDelegate <NSObject>
- (void)bridge:(ATMinimalNativeBridge*)bridge
    loadWindow:(uint64_t)token;
- (void)bridge:(ATMinimalNativeBridge*)bridge
    syncWindow:(uint64_t)token;
- (void)bridge:(ATMinimalNativeBridge*)bridge
    unloadWindow:(uint64_t)token;
- (void)bridge:(ATMinimalNativeBridge*)bridge
    loadButton:(uint64_t)token;
- (void)bridge:(ATMinimalNativeBridge*)bridge
    syncButton:(uint64_t)token;
- (void)bridge:(ATMinimalNativeBridge*)bridge
    unloadButton:(uint64_t)token;
- (void)bridge:(ATMinimalNativeBridge*)bridge
    loadList:(uint64_t)token;
- (void)bridge:(ATMinimalNativeBridge*)bridge
    syncList:(uint64_t)token;
- (void)bridge:(ATMinimalNativeBridge*)bridge
    unloadList:(uint64_t)token;
- (void)bridgeModelDidFinish:(ATMinimalNativeBridge*)bridge;
@end

@interface ATMinimalWindowState : NSObject
@property(nonatomic, readonly) NSString* title;
@property(nonatomic, readonly) double x;
@property(nonatomic, readonly) double y;
@property(nonatomic, readonly) double width;
@property(nonatomic, readonly) double height;
@end

@interface ATMinimalButtonState : NSObject
@property(nonatomic, readonly) uint64_t parentToken;
@property(nonatomic, readonly) NSString* title;
@property(nonatomic, readonly, getter=isEnabled) BOOL enabled;
@property(nonatomic, readonly) double x;
@property(nonatomic, readonly) double y;
@property(nonatomic, readonly) double width;
@property(nonatomic, readonly) double height;
@end

@interface ATMinimalListState : NSObject
@property(nonatomic, readonly) uint64_t parentToken;
@property(nonatomic, readonly) NSArray<NSString*>* rows;
@property(nonatomic, readonly) NSInteger selectedRow;
@property(nonatomic, readonly) double x;
@property(nonatomic, readonly) double y;
@property(nonatomic, readonly) double width;
@property(nonatomic, readonly) double height;
@end

@interface ATMinimalNativeBridge : NSObject
@property(nonatomic, weak, readonly)
    id<ATMinimalNativeBridgeDelegate> delegate;
- (instancetype)initWithUserDirectory:(NSString*)userDirectory
                             delegate:(id<ATMinimalNativeBridgeDelegate>)delegate;
- (void)start;
- (void)consumePublication:(ATMinimalPublicationKind)kind;
- (ATMinimalWindowState*)windowState:(uint64_t)token;
- (ATMinimalButtonState*)buttonState:(uint64_t)token;
- (ATMinimalListState*)listState:(uint64_t)token;
- (void)clickButton:(uint64_t)token;
- (void)selectListRow:(NSInteger)row token:(uint64_t)token;
- (void)submitWindowX:(double)x
                    y:(double)y
                width:(double)width
               height:(double)height
                token:(uint64_t)token;
- (BOOL)requestCloseWindow:(uint64_t)token;
- (void)requestStop;
- (void)finishTeardown;
@end

NS_ASSUME_NONNULL_END
