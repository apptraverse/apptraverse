#ifndef APPTRAVERSE_NO_RTTI_H_
#define APPTRAVERSE_NO_RTTI_H_

// AppTraverse must compile with C++ RTTI disabled (/GR- or -fno-rtti).
#if defined(_CPPRTTI)
#  error "AppTraverse must be built with RTTI disabled (/GR-)"
#endif
#if defined(__GXX_RTTI)
#  error "AppTraverse must be built with RTTI disabled (-fno-rtti)"
#endif

#endif  // APPTRAVERSE_NO_RTTI_H_
