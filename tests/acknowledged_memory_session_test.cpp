#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "aether/clock.h"
#include "aether/domain_storage/ram_domain_storage.h"
#include "aether/obj/obj.h"

#include "apptraverse/event_for.h"
#include "apptraverse/node_for.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/object_graph_copy.h"
#include "apptraverse/shared_graph_sync_session.h"
#include "apptraverse/sync_packet.h"
#include "apptraverse/unreliable_memory_link.h"
#include "model/chat.h"
#include "model/chat_events.h"
#include "model/chat_presenter.h"
#include "model/client.h"
#include "model/registration.h"

namespace apptraverse::test {

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"       \
                << __LINE__ << '\n';                                         \
      std::exit(1);                                                          \
    }                                                                        \
  } while (0)

class SessionDoc;
class SessionBumpEvent;

class SessionDoc : public NodeFor<SessionDoc> {
  APPTRAVERSE_OBJECT(SessionDoc, Node, 0)

 protected:
  SessionDoc() = default;

 public:
  explicit SessionDoc(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(value))

  std::int32_t value{0};

  void Apply(SessionBumpEvent const& event);
};

class SessionBumpEvent : public EventFor<SessionDoc, SessionBumpEvent> {
  APPTRAVERSE_OBJECT(SessionBumpEvent, Event, 0)

 protected:
  SessionBumpEvent() = default;

 public:
  explicit SessionBumpEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(delta))

  std::int32_t delta{0};
};

void SessionDoc::Apply(SessionBumpEvent const& event) { value += event.delta; }

APPTRAVERSE_REGISTER(SessionDoc);
APPTRAVERSE_REGISTER(SessionBumpEvent);

struct SessionReplica {
  ae::RamDomainStorage storage;
  ae::Domain domain;
  SessionDoc::ptr doc;
  SessionDoc::ptr doc_base;

  SessionReplica() : domain{ae::Now(), storage} {
    doc_base = SessionDoc::ptr::Create(ae::CreateWith{domain}.with_id(10));
    doc = SessionDoc::ptr::Create(ae::CreateWith{domain}.with_id(11));
    doc->base = doc_base;
    doc->CaptureBaseState();
    doc.Save();
  }

  SyncReplica AsSyncReplica() {
    return SyncReplica{domain, storage, doc.id()};
  }

  void CommitBump(std::int32_t delta, ae::ObjId::Type event_id) {
    auto event =
        SessionBumpEvent::ptr::Create(ae::CreateWith{domain}.with_id(event_id));
    event->delta = delta;
    doc->Commit(event);
    doc.Save();
  }
};

bool IsAckEnvelope(SerializedSyncPacket const& bytes) {
  auto decoded = SyncPacketCodec{}.Decode(bytes);
  return decoded.packet->GetClassId() == AckPacket::kClassId;
}

std::size_t FindFirstAck(UnreliableMemoryLink const& link) {
  for (std::size_t i = 0; i < link.pending_count(); ++i) {
    if (IsAckEnvelope(link.envelopes()[i].bytes)) {
      return i;
    }
  }
  return link.pending_count();
}

std::size_t FindFirstNonAck(UnreliableMemoryLink const& link) {
  for (std::size_t i = 0; i < link.pending_count(); ++i) {
    if (!IsAckEnvelope(link.envelopes()[i].bytes)) {
      return i;
    }
  }
  return link.pending_count();
}

std::size_t FindEventPacketByRoot(UnreliableMemoryLink const& link,
                                  ae::ObjId::Type root_id) {
  for (std::size_t i = 0; i < link.pending_count(); ++i) {
    auto decoded = SyncPacketCodec{}.Decode(link.envelopes()[i].bytes);
    if (decoded.packet->GetClassId() != EventPacket::kClassId) {
      continue;
    }
    auto event_packet = EventPacket::ptr{decoded.packet};
    if (event_packet->event.id().id() == root_id) {
      return i;
    }
  }
  return link.pending_count();
}

void TestReliableDelivery() {
  SessionReplica left;
  SessionReplica right;
  UnreliableMemoryLink link;
  SharedGraphSyncSession left_session{left.AsSyncReplica(),
                                      link.MakeSend(0)};
  SharedGraphSyncSession right_session{right.AsSyncReplica(),
                                       link.MakeSend(1)};
  link.Bind(left_session, right_session);

  left_session.StartInitialSynchronization();
  CHECK(left_session.pending_packet_count() == 1);
  link.DeliverAllInOrder();
  CHECK(left_session.pending_packet_count() == 0);
  CHECK(left_session.initial_sync_complete());
  right.doc.Load();
  CHECK(right.doc.is_loaded());
}

void TestLostPacketRetry() {
  SessionReplica left;
  SessionReplica right;
  UnreliableMemoryLink link;
  SharedGraphSyncSession left_session{left.AsSyncReplica(),
                                      link.MakeSend(0)};
  SharedGraphSyncSession right_session{right.AsSyncReplica(),
                                       link.MakeSend(1)};
  link.Bind(left_session, right_session);

  left_session.StartInitialSynchronization();
  CHECK(link.pending_count() == 1);
  link.Drop(0);
  CHECK(left_session.pending_packet_count() == 1);

  left_session.RetryPending();
  CHECK(link.pending_count() == 1);
  link.DeliverAllInOrder();
  CHECK(left_session.pending_packet_count() == 0);
  right.doc.Load();
  CHECK(right.doc.is_loaded());
}

void TestLostAckRetry() {
  SessionReplica left;
  left.CommitBump(1, 100);
  SessionReplica right;
  UnreliableMemoryLink link;
  SharedGraphSyncSession left_session{left.AsSyncReplica(),
                                      link.MakeSend(0)};
  SharedGraphSyncSession right_session{right.AsSyncReplica(),
                                       link.MakeSend(1)};
  link.Bind(left_session, right_session);

  left_session.StartInitialSynchronization();
  link.Deliver(FindFirstNonAck(link));
  auto const ack_index = FindFirstAck(link);
  CHECK(ack_index < link.pending_count());
  link.Drop(ack_index);
  CHECK(left_session.pending_packet_count() == 1);

  right.doc.Load();
  CHECK(right.doc->value == 1);
  auto const journal_size = right.doc->journal.size();

  left_session.RetryPending();
  link.Deliver(FindFirstNonAck(link));
  right.doc.Load();
  CHECK(right.doc->journal.size() == journal_size);
  CHECK(right.doc->value == 1);
  link.DeliverAllInOrder();
  CHECK(left_session.pending_packet_count() == 0);
}

void TestDuplicatePacket() {
  SessionReplica left;
  left.CommitBump(2, 200);
  SessionReplica right;
  UnreliableMemoryLink link;
  SharedGraphSyncSession left_session{left.AsSyncReplica(),
                                      link.MakeSend(0)};
  SharedGraphSyncSession right_session{right.AsSyncReplica(),
                                       link.MakeSend(1)};
  link.Bind(left_session, right_session);

  left_session.StartInitialSynchronization();
  link.Duplicate(0);
  CHECK(link.pending_count() == 2);
  link.DeliverAllInOrder();
  right.doc.Load();
  CHECK(right.doc->value == 2);
  CHECK(right.doc->journal.size() == 1);
  CHECK(left_session.pending_packet_count() == 0);
}

void TestBlockedPacketNoAck() {
  ae::RamDomainStorage left_storage;
  ae::Domain left_domain{ae::Now(), left_storage};
  auto left_client_base =
      Client::ptr::Create(ae::CreateWith{left_domain}.with_id(201));
  auto left_client =
      Client::ptr::Create(ae::CreateWith{left_domain}.with_id(202));
  left_client->name = "Alice";
  left_client->base = left_client_base;
  left_client->CaptureBaseState();
  left_client.Save();
  auto left_chat_base =
      Chat::ptr::Create(ae::CreateWith{left_domain}.with_id(101));
  auto left_chat =
      Chat::ptr::Create(ae::CreateWith{left_domain}.with_id(102));
  left_chat->base = left_chat_base;
  left_chat->CaptureBaseState();
  left_chat.Save();

  ae::RamDomainStorage right_storage;
  ae::Domain right_domain{ae::Now(), right_storage};
  SyncReplica right_seed{right_domain, right_storage, ae::ObjId{102}};
  ImportObjectGraph(left_client, left_storage, right_seed,
                    SharedCopyMode::kCopyLoadedTargets);
  auto right_chat_base =
      Chat::ptr::Create(ae::CreateWith{right_domain}.with_id(101));
  auto right_chat =
      Chat::ptr::Create(ae::CreateWith{right_domain}.with_id(102));
  right_chat->base = right_chat_base;
  right_chat->CaptureBaseState();
  right_chat.Save();

  UnreliableMemoryLink link;
  SyncReplica left_rep{left_domain, left_storage, left_chat.id()};
  SyncReplica right_rep{right_domain, right_storage, right_chat.id()};
  SharedGraphSyncSession left_session{left_rep, link.MakeSend(0)};
  SharedGraphSyncSession right_session{right_rep, link.MakeSend(1)};
  link.Bind(left_session, right_session);

  left_session.StartInitialSynchronization();
  link.DeliverAllInOrder();
  CHECK(left_session.initial_sync_complete());
  CHECK(left_session.pending_packet_count() == 0);

  auto join =
      JoinClientEvent::ptr::Create(ae::CreateWith{left_domain}.with_id(301));
  join->client = left_client;
  left_chat->Commit(join);
  auto message =
      AddMessageEvent::ptr::Create(ae::CreateWith{left_domain}.with_id(501));
  message->author = left_client;
  message->text = "later";
  left_chat->Commit(message);
  left_chat.Save();

  left_session.Poll();
  // Join Event + Message Event (Client travels inside Event graphs).
  CHECK(left_session.pending_packet_count() == 2);

  auto message_index = FindEventPacketByRoot(link, 501);
  CHECK(message_index < link.pending_count());
  link.Deliver(message_index);
  CHECK(FindFirstAck(link) == link.pending_count());
  right_chat.Load();
  CHECK(right_chat->journal.empty());
  CHECK(left_session.pending_packet_count() == 2);

  auto join_index = FindEventPacketByRoot(link, 301);
  CHECK(join_index < link.pending_count());
  link.Deliver(join_index);
  link.DeliverAllInOrder();
  CHECK(left_session.pending_packet_count() == 1);

  left_session.RetryPending();
  link.DeliverAllInOrder();
  CHECK(left_session.pending_packet_count() == 0);
  right_chat.Load();
  CHECK(right_chat->journal.size() == 2);
  CHECK(right_chat->entries.size() == 2);
}

}  // namespace apptraverse::test

int main() {
  apptraverse::EnsureObjectRegistration();
  apptraverse::EnsureSingleClientChatRegistration();
  apptraverse::test::TestReliableDelivery();
  apptraverse::test::TestLostPacketRetry();
  apptraverse::test::TestLostAckRetry();
  apptraverse::test::TestDuplicatePacket();
  apptraverse::test::TestBlockedPacketNoAck();
  std::cout << "acknowledged_memory_session_test OK\n";
  return 0;
}
