#import <Foundation/Foundation.h>
#import <TargetConditionals.h>
NS_ASSUME_NONNULL_BEGIN
@interface ATChatBridge : NSObject
- (instancetype)initWithProfile:(NSString*)profile name:(NSString*)name changed:(void (^)(void))changed;
- (BOOL)start;
- (void)stop;
- (void)checkpoint:(void (^)(BOOL))completion;
- (NSDictionary*)snapshot;
- (void)createRoom;
- (void)joinRoom:(NSString*)invite;
- (void)send:(NSString*)text;
- (void)send:(NSString*)text committed:(void (^)(BOOL))completion;
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
