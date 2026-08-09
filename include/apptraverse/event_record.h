#ifndef APPTRAVERSE_EVENT_RECORD_H_
#define APPTRAVERSE_EVENT_RECORD_H_

#include <cstdint>

#include "aether-miscpp/reflect/reflect.h"

#include "apptraverse/event.h"

namespace apptraverse {

struct EventRecord {
  std::uint64_t timestamp_us{0};
  Event::ptr event;

  AE_REFLECT_MEMBERS(timestamp_us, event)
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_EVENT_RECORD_H_
