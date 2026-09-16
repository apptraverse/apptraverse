#ifndef APPTRAVERSE_EXAMPLE_CHAT_DEMO_PROFILE_LOCK_H_
#define APPTRAVERSE_EXAMPLE_CHAT_DEMO_PROFILE_LOCK_H_

#include <cstdint>
#include <filesystem>

namespace apptraverse::example::chat_demo {

// OS-backed exclusive profile lock. One owner per profile; held until release.
class ProfileLock {
 public:
  enum class AcquireResult : std::uint8_t {
    kAcquired = 0,
    kBusy = 1,
    kError = 2,
  };

  ProfileLock() = default;
  ProfileLock(ProfileLock&& other) noexcept;
  ProfileLock& operator=(ProfileLock&& other) noexcept;
  ~ProfileLock();

  ProfileLock(ProfileLock const&) = delete;
  ProfileLock& operator=(ProfileLock const&) = delete;

  [[nodiscard]] bool owns_lock() const noexcept { return owns_; }

  static AcquireResult TryAcquire(std::filesystem::path const& state_dir,
                                  ProfileLock& out);

 private:
  void Release() noexcept;

#ifdef _WIN32
  void* handle_{nullptr};
#else
  int fd_{-1};
#endif
  bool owns_{false};
};

}  // namespace apptraverse::example::chat_demo

#endif  // APPTRAVERSE_EXAMPLE_CHAT_DEMO_PROFILE_LOCK_H_
