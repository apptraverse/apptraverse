#import <AppKit/AppKit.h>
#import "ChatBridge.h"
static void Walk(id element, NSMutableSet* visited, void (^visit)(id)) {
  if (!element) return;
  auto key = [NSValue valueWithPointer:(__bridge void*)element];
  if ([visited containsObject:key]) return;
  [visited addObject:key]; visit(element);
  if ([element respondsToSelector:@selector(accessibilityChildren)]) {
    for (id child in [(id<NSAccessibility>)element accessibilityChildren]) Walk(child, visited, visit);
  }
  if ([element isKindOfClass:NSView.class]) {
    for (NSView* child in [(NSView*)element subviews]) Walk(child, visited, visit);
  }
}
BOOL ATChatPress(id root, NSString* identifier) {
  __block BOOL pressed = NO;
  Walk(root, [NSMutableSet set], ^(id element) {
    if (!pressed && [element respondsToSelector:@selector(accessibilityIdentifier)] &&
        [[(id<NSAccessibility>)element accessibilityIdentifier] isEqual:identifier] &&
        [element respondsToSelector:@selector(accessibilityPerformPress)]) {
      pressed = [(id<NSAccessibility>)element accessibilityPerformPress];
    }
  });
  return pressed;
}
NSArray<NSString*>* ATChatVisibleStrings(id root) {
  NSMutableArray* strings = [NSMutableArray array];
  Walk(root, [NSMutableSet set], ^(id element) {
    for (NSString* selector in @[@"accessibilityValue", @"accessibilityLabel"]) {
      SEL sel = NSSelectorFromString(selector);
      if ([element respondsToSelector:sel]) {
        id value = ((id (*)(id, SEL))[element methodForSelector:sel])(element, sel);
        if ([value isKindOfClass:NSString.class] && [value length]) [strings addObject:value];
      }
    }
  });
  return strings;
}

NSRect ATChatFrame(id root, NSString* identifier) {
  __block NSRect frame = NSZeroRect;
  Walk(root, [NSMutableSet set], ^(id element) {
    if ([element respondsToSelector:@selector(accessibilityIdentifier)] &&
        [[(id<NSAccessibility>)element accessibilityIdentifier] isEqual:identifier] &&
        [element respondsToSelector:@selector(accessibilityFrame)]) {
      frame = [(id<NSAccessibility>)element accessibilityFrame];
    }
  });
  return frame;
}

BOOL ATChatEnabled(id root, NSString* identifier) {
  __block BOOL enabled = NO;
  Walk(root, [NSMutableSet set], ^(id element) {
    if ([element respondsToSelector:@selector(accessibilityIdentifier)] &&
        [[(id<NSAccessibility>)element accessibilityIdentifier] isEqual:identifier] &&
        [element respondsToSelector:@selector(isAccessibilityEnabled)]) {
      enabled = [(id<NSAccessibility>)element isAccessibilityEnabled];
    }
  });
  return enabled;
}

// SwiftUI materializes its accessibility nodes only when accessibility is
// active. Enable it for this test host; this legacy setter has no typed peer.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
void ATChatEnableAccessibility(void) {
  [NSApp accessibilitySetValue:@YES forAttribute:@"AXManualAccessibility"];
  [NSApp accessibilitySetValue:@YES forAttribute:@"AXEnhancedUserInterface"];
}
#pragma clang diagnostic pop
