#include "profile_lock.h"

#include <filesystem>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#else
#  include <fcntl.h>
#  include <sys/file.h>
#  include <unistd.h>
#endif

namespace apptraverse::example::chat_demo {

ProfileLock::ProfileLock(ProfileLock&& other) noexcept : owns_{other.owns_} {
#ifdef _WIN32
  handle_ = other.handle_;
  other.handle_ = nullptr;
#else
  fd_ = other.fd_;
  other.fd_ = -1;
#endif
  other.owns_ = false;
}

ProfileLock& ProfileLock::operator=(ProfileLock&& other) noexcept {
  if (this != &other) {
    Release();
    owns_ = other.owns_;
#ifdef _WIN32
    handle_ = other.handle_;
    other.handle_ = nullptr;
#else
    fd_ = other.fd_;
    other.fd_ = -1;
#endif
    other.owns_ = false;
  }
  return *this;
}

ProfileLock::~ProfileLock() { Release(); }

void ProfileLock::Release() noexcept {
  if (!owns_) {
    return;
  }
#ifdef _WIN32
  CloseHandle(static_cast<HANDLE>(handle_));
  handle_ = nullptr;
#else
  close(fd_);
  fd_ = -1;
#endif
  owns_ = false;
}

ProfileLock::AcquireResult ProfileLock::TryAcquire(
    std::filesystem::path const& state_dir, ProfileLock& out) {
  out.Release();
  std::filesystem::create_directories(state_dir);
  std::filesystem::path const lock_file = state_dir / "profile.lock";

#ifdef _WIN32
  HANDLE const handle =
      CreateFileW(lock_file.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                  CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    DWORD const err = GetLastError();
    if (err == ERROR_SHARING_VIOLATION || err == ERROR_LOCK_VIOLATION) {
      return AcquireResult::kBusy;
    }
    return AcquireResult::kError;
  }
  out.handle_ = handle;
  out.owns_ = true;
  return AcquireResult::kAcquired;
#else
  int const fd = open(lock_file.c_str(), O_RDWR | O_CREAT, 0600);
  if (fd < 0) {
    return AcquireResult::kError;
  }
  if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
    close(fd);
    return AcquireResult::kBusy;
  }
  out.fd_ = fd;
  out.owns_ = true;
  return AcquireResult::kAcquired;
#endif
}

}  // namespace apptraverse::example::chat_demo
