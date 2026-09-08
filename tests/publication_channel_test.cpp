#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "apptraverse/publication_channel.h"

namespace apptraverse::test {

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"       \
                << __LINE__ << '\n';                                        \
      std::exit(1);                                                          \
    }                                                                        \
  } while (0)

void WriteMarker(PublicationBuffer* buffer, char marker) {
  buffer->sink.write(&marker, 1);
}

char ReadMarker(PublicationBuffer const* buffer) {
  CHECK(buffer != nullptr);
  CHECK(buffer->sink.bytes.size() == 1);
  return static_cast<char>(buffer->sink.bytes[0]);
}

void TestUnreadMustNotBeOverwritten() {
  PublicationChannel<3> channel;
  auto* first = channel.AcquireProducer();
  CHECK(first != nullptr);
  WriteMarker(first, 'A');
  channel.PublishProducer();
  CHECK(channel.has_unread_published());

  auto* second = channel.AcquireProducer();
  CHECK(second != nullptr);
  CHECK(second != first);
  WriteMarker(second, 'B');

  auto* held = channel.TakePublished();
  CHECK(ReadMarker(held) == 'A');
  channel.ReleaseConsumer();
}

void TestConsumerBufferSurvivesNextPublish() {
  PublicationChannel<3> channel;
  auto* first = channel.AcquireProducer();
  WriteMarker(first, 'A');
  channel.PublishProducer();

  auto* held = channel.TakePublished();
  CHECK(ReadMarker(held) == 'A');

  auto* second = channel.AcquireProducer();
  CHECK(second != held);
  WriteMarker(second, 'B');
  channel.PublishProducer();

  CHECK(ReadMarker(held) == 'A');
  channel.ReleaseConsumer();

  auto copy = channel.TakePublishedCopy();
  CHECK(copy.size() == 1);
  CHECK(copy[0] == 'B');
  CHECK(!channel.has_unread_published());
}

void TestTakePublishedCopyReleasesSlot() {
  PublicationChannel<3> channel;
  auto* first = channel.AcquireProducer();
  WriteMarker(first, 'X');
  channel.PublishProducer();

  auto copy = channel.TakePublishedCopy();
  CHECK(copy.size() == 1);
  CHECK(copy[0] == 'X');
  CHECK(!channel.has_unread_published());

  auto* again = channel.AcquireProducer();
  CHECK(again != nullptr);
  WriteMarker(again, 'Y');
  channel.PublishProducer();
  auto second = channel.TakePublishedCopy();
  CHECK(copy.size() == 1);
  CHECK(copy[0] == 'X');
  CHECK(second.size() == 1);
  CHECK(second[0] == 'Y');
}

void TestProducerCannotClearInUiSlot() {
  PublicationChannel<3> channel;
  auto* first = channel.AcquireProducer();
  WriteMarker(first, 'A');
  channel.PublishProducer();

  std::atomic<PublicationBuffer*> held{nullptr};
  std::atomic<bool> consumer_ready{false};
  std::atomic<bool> producer_done{false};

  std::thread consumer{[&] {
    auto* buffer = channel.TakePublished();
    held.store(buffer);
    consumer_ready.store(true);
    while (!producer_done.load()) {
      CHECK(ReadMarker(buffer) == 'A');
    }
    CHECK(ReadMarker(buffer) == 'A');
    channel.ReleaseConsumer();
  }};

  while (!consumer_ready.load()) {
  }

  for (int i = 0; i < 1000; ++i) {
    auto* next = channel.AcquireProducer();
    CHECK(next != nullptr);
    CHECK(next != held.load());
    WriteMarker(next, 'B');
    if (!channel.has_unread_published()) {
      channel.PublishProducer();
    }
  }

  producer_done.store(true);
  consumer.join();

  auto leftover = channel.TakePublishedCopy();
  CHECK(leftover.size() == 1);
  CHECK(leftover[0] == 'B');
}

}  // namespace apptraverse::test

int main() {
  apptraverse::test::TestUnreadMustNotBeOverwritten();
  apptraverse::test::TestConsumerBufferSurvivesNextPublish();
  apptraverse::test::TestTakePublishedCopyReleasesSlot();
  apptraverse::test::TestProducerCannotClearInUiSlot();
  std::cout << "publication_channel_test OK\n";
  return 0;
}
