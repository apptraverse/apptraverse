#pragma once

#import "apptraverse/platform/apple/ATAppleWindowLayout.h"

#include "apptraverse/ui/ui_constraint_layout.h"

@interface ATAppleWindowLayout ()
- (instancetype)initWithRoot:(apptraverse::ui::UiLayoutNode const&)root;
@end
