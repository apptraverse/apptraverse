#ifndef APPTRAVERSE_SHARED_EVENT_ID_H_
#define APPTRAVERSE_SHARED_EVENT_ID_H_

#include <cstdint>
#include <string>

namespace apptraverse {

// Stable global identity for a shared journal Event across replicas.
struct SharedEventId {
  std::string origin_uid;
  std::uint64_t origin_sequence{0};

  bool operator==(SharedEventId const& other) const noexcept {
    return origin_sequence == other.origin_sequence &&
           origin_uid == other.origin_uid;
  }

  bool operator!=(SharedEventId const& other) const noexcept {
    return !(*this == other);
  }
};

inline bool SharedEventIdLess(SharedEventId const& a,
                              SharedEventId const& b) noexcept {
  if (a.origin_uid != b.origin_uid) {
    return a.origin_uid < b.origin_uid;
  }
  return a.origin_sequence < b.origin_sequence;
}

}  // namespace apptraverse

#endif  // APPTRAVERSE_SHARED_EVENT_ID_H_
