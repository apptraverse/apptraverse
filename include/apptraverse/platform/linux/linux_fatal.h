#ifndef APPTRAVERSE_PLATFORM_LINUX_LINUX_FATAL_H_
#define APPTRAVERSE_PLATFORM_LINUX_LINUX_FATAL_H_

namespace apptraverse {

[[noreturn]] void FatalLinux(char const* operation);

}  // namespace apptraverse

#endif  // APPTRAVERSE_PLATFORM_LINUX_LINUX_FATAL_H_
