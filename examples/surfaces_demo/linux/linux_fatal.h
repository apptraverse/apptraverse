#ifndef APPTRAVERSE_SURFACES_LINUX_FATAL_H_
#define APPTRAVERSE_SURFACES_LINUX_FATAL_H_

namespace apptraverse {

// Shared Linux fatal path for this example. Does not return.
[[noreturn]] void FatalLinux(char const* operation);

}  // namespace apptraverse

#endif  // APPTRAVERSE_SURFACES_LINUX_FATAL_H_
