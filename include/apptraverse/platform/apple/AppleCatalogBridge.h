#ifndef APPTRAVERSE_PLATFORM_APPLE_CATALOG_BRIDGE_H_
#define APPTRAVERSE_PLATFORM_APPLE_CATALOG_BRIDGE_H_

#import <Foundation/Foundation.h>

// SwiftUI sees only this Objective-C projection. It does not include Æther
// or any C++ header. Actions are intents; publication replaces the data.

NS_ASSUME_NONNULL_BEGIN

@interface AppleButtonProjection : NSObject
@property(nonatomic) uint32_t controlId;
@property(nonatomic, copy) NSString* title;
@property(nonatomic) BOOL enabled;
@end

@interface AppleWindowProjection : NSObject
@property(nonatomic) uint64_t windowId;
@property(nonatomic, copy) NSString* title;
@property(nonatomic) BOOL catalogChrome;
@property(nonatomic) NSInteger selection;
@property(nonatomic, copy) NSArray<NSString*>* rowTitles;
@property(nonatomic, copy) NSArray<NSString*>* rowIds;
@property(nonatomic, copy) NSArray<AppleButtonProjection*>* buttons;
@property(nonatomic) int32_t frameX;
@property(nonatomic) int32_t frameY;
@property(nonatomic) int32_t frameWidth;
@property(nonatomic) int32_t frameHeight;
@end

@interface AppleCatalogProjection : NSObject
@property(nonatomic, copy) NSArray<AppleWindowProjection*>* windows;
@property(nonatomic, copy) NSString* saveStatus;
@property(nonatomic) uint64_t revision;
@property(nonatomic, copy) NSString* viewportLabel;
@property(nonatomic, copy) NSString* sessionLabel;
@property(nonatomic, copy, nullable) void (^onChanged)(void);
@property(nonatomic, copy, nullable) void (^onButton)(uint64_t windowId,
                                                      uint32_t controlId);
@property(nonatomic, copy, nullable) void (^onSelectRow)(uint64_t windowId,
                                                         NSInteger row);
@property(nonatomic, copy, nullable) void (^onCloseWindow)(uint64_t windowId);
@property(nonatomic, copy, nullable) void (^onSave)(void);
@property(nonatomic, copy, nullable) void (^onViewport)(int32_t width,
                                                        int32_t height);
- (void)noteChanged;
@end

NS_ASSUME_NONNULL_END

#endif  // APPTRAVERSE_PLATFORM_APPLE_CATALOG_BRIDGE_H_
