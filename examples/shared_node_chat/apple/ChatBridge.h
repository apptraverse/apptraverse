#import <Foundation/Foundation.h>
#import <TargetConditionals.h>
#if TARGET_OS_IOS
#import "apptraverse/platform/ios/ATIosApplicationBridge.h"
#endif
NS_ASSUME_NONNULL_BEGIN
@interface ATChatBridge : NSObject
- (instancetype)initWithProfile:(NSString*)profile name:(NSString*)name host:(BOOL)host initialHost:(NSString*)initialHost loadOnly:(BOOL)loadOnly changed:(void (^)(void))changed;
- (void)registerApplePushToken:(NSString*)token;
- (BOOL)start;
- (void)stop;
- (void)checkpoint:(void (^)(BOOL))completion;
- (NSDictionary*)snapshot;
- (void)editDraft:(NSString*)text;
- (void)editInvite:(NSString*)text;
- (void)join;
- (void)send;
- (void)copyID;
- (void)clipboardResult:(BOOL)success;
#if TARGET_OS_OSX
- (void)observeEnvironment;
- (void)observeWindow:(id)window commit:(BOOL)commit;
- (BOOL)applyWindowPlacement:(id)window;
- (void)resetWindowPlacement;
#else
- (void)observeMobileSettings:(ATIosDeviceSettings)settings;
- (void)reportMobileViewportWidth:(int)width height:(int)height
    NS_SWIFT_NAME(reportMobileViewport(width:height:));
#endif
@end
NS_ASSUME_NONNULL_END
#if TARGET_OS_OSX
// Implemented only by the separate native acceptance executable.
FOUNDATION_EXPORT BOOL ATChatPress(id _Nonnull root, NSString* _Nonnull identifier);
FOUNDATION_EXPORT NSArray<NSString*>* _Nonnull ATChatVisibleStrings(id _Nonnull root);
FOUNDATION_EXPORT NSRect ATChatFrame(id _Nonnull root, NSString* _Nonnull identifier);
FOUNDATION_EXPORT BOOL ATChatEnabled(id _Nonnull root, NSString* _Nonnull identifier);
FOUNDATION_EXPORT void ATChatEnableAccessibility(void);

#endif
