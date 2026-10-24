#ifndef APPTRAVERSE_NO_EXCEPTIONS_H_
#define APPTRAVERSE_NO_EXCEPTIONS_H_

#if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND)
#error "AppTraverse must be compiled without C++ exception handling"
#endif
#if defined(_MSC_VER) && (!defined(_HAS_EXCEPTIONS) || _HAS_EXCEPTIONS != 0)
#error "AppTraverse MSVC consumers must set _HAS_EXCEPTIONS=0"
#endif

#endif  // APPTRAVERSE_NO_EXCEPTIONS_H_
