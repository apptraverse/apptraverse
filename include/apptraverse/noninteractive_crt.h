#ifndef APPTRAVERSE_NONINTERACTIVE_CRT_H_
#define APPTRAVERSE_NONINTERACTIVE_CRT_H_

namespace apptraverse {

// Process-local CRT/WER setup so Debug assert/_ASSERTE abort with a non-zero
// exit instead of Abort/Retry/Ignore or JIT/WER dialogs. Call before other
// initialization. Does not define NDEBUG and does not swallow asserts.
void EnableNoninteractiveCrt();

// One diagnostic write: CRT stderr when attached, otherwise Win32
// STD_ERROR_HANDLE (GUI-subsystem children with redirected stderr).
void WriteFatalStderr(char const* text);

}  // namespace apptraverse

#endif  // APPTRAVERSE_NONINTERACTIVE_CRT_H_
