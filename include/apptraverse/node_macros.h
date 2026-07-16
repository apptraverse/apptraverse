#ifndef APPTRAVERSE_NODE_MACROS_H_
#define APPTRAVERSE_NODE_MACROS_H_

#include "apptraverse/node.h"

#define AT_NODE_OBJECT(DERIVED, BASE, VERSION) \
  AE_OBJECT(DERIVED, BASE, VERSION)            \
                                               \
 public:                                       \
  AE_OBJECT_REFLECT()                          \
                                               \
 private:                                      \
  using AppTraverseNodeSelf = DERIVED;

#define AT_NODE_STATE(...)                                    \
 public:                                                      \
  template <typename Dnv>                                     \
  void Load(CurrentVersion, Dnv& dnv) {                       \
    dnv(base_);                                               \
    if (ShouldTransferBusinessState()) {                      \
      dnv(__VA_ARGS__);                                       \
    }                                                         \
    FinishLoadIfMostDerived<AppTraverseNodeSelf>();           \
  }                                                           \
                                                              \
  template <typename Dnv>                                     \
  void Save(CurrentVersion, Dnv& dnv) const {                 \
    dnv(base_);                                               \
    if (ShouldTransferBusinessState()) {                      \
      dnv(__VA_ARGS__);                                       \
    }                                                         \
  }                                                           \
                                                              \
 private:

#endif  // APPTRAVERSE_NODE_MACROS_H_
