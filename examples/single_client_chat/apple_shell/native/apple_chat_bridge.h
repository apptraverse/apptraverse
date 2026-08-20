#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

@interface ATAppleChatBridge : NSObject

- (instancetype)initWithStateDirectory:(NSString *)stateDirectory
                         clientName:(NSString *)clientName
                    localClientName:(NSString *)localClientName;

@property (atomic, copy, readonly) NSString *localUID;
@property (atomic, copy, readonly) NSArray<NSString *> *timelineRows;

- (void)setOnChange:(void (^)(void))block;
- (void)start;
- (void)stop;
- (void)addPeerWithUID:(NSString *)uid;
- (void)submitText:(NSString *)text;

@end

NS_ASSUME_NONNULL_END
