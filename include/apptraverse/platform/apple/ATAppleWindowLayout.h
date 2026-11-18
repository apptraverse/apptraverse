#pragma once

#import <CoreGraphics/CoreGraphics.h>
#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

@interface ATAppleLayoutResult : NSObject
@property(nonatomic, readonly) CGSize contentSize;
- (CGRect)frameForControl:(uint32_t)controlID NS_SWIFT_NAME(frame(forControl:));
@end

// An immutable constraint snapshot. Width -1 requests an ideal measurement,
// and width 0 requests the control's minimum width.
@interface ATAppleWindowLayout : NSObject
- (ATAppleLayoutResult*)solveViewport:(CGSize)viewport
                        controlIDs:(NSArray<NSNumber*>*)controlIDs
                           measure:(CGSize (^)(uint32_t controlID, double width))measure;
@end

NS_ASSUME_NONNULL_END
