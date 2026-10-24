#include "surfaces_embedded_release.h"

#if defined(_WIN32)
extern "C" __declspec(dllimport) void* __stdcall GetStdHandle(unsigned long id);
extern "C" __declspec(dllimport) int __stdcall WriteFile(void* file, const void* buffer,
                                                           unsigned long bytes_to_write,
                                                           unsigned long* bytes_written,
                                                           void* overlapped);
#else
extern "C" int write(int fd, const void* buf, unsigned long count);
#endif

namespace {

void write_unsigned_line(unsigned value) {
  char buf[12];
  int len = 0;
  if (value == 0) {
    buf[len++] = '0';
  } else {
    char rev[12];
    int rev_len = 0;
    while (value > 0) {
      rev[rev_len++] = static_cast<char>('0' + (value % 10));
      value /= 10;
    }
    while (rev_len > 0) {
      buf[len++] = rev[--rev_len];
    }
  }
  buf[len++] = '\n';

#if defined(_WIN32)
  constexpr unsigned long kStdOutputHandle = static_cast<unsigned long>(-11);
  unsigned long written = 0;
  void* stdout_handle = GetStdHandle(kStdOutputHandle);
  WriteFile(stdout_handle, buf, static_cast<unsigned long>(len), &written, nullptr);
#else
  write(1, buf, static_cast<unsigned long>(len));
#endif
}

}  // namespace

int main() {
  write_unsigned_line(apptraverse::surfaces_demo::kEmbeddedRelease);
  return 0;
}
