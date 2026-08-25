#include "chat_component.h"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <optional>
#include <utility>

#include "aether-miscpp/format/format.h"

#include "model/chat_events.h"
#include "model/chat_presenter.h"

#include "apptraverse/runtime_trace.h"

namespace apptraverse::chat {
using ::apptraverse::AssertBusinessThread;
using ::apptraverse::Trace;
namespace {

std::string TrimWhitespace(std::string text) {
  auto not_space = [](unsigned char ch) {
    return !std::isspace(ch);
  };
  text.erase(text.begin(),
             std::find_if(text.begin(), text.end(), not_space));
  text.erase(std::find_if(text.rbegin(), text.rend(), not_space).base(),
             text.end());
  return text;
}

ChatComponent::LocalJoinProbe ProbeLocalJoinInJournal(
    Chat::ptr const& chat, Client::ptr const& local_client) {
  ChatComponent::LocalJoinProbe out{};
  if (!chat.is_loaded() || !local_client.is_valid()) {
    return out;
  }
  local_client.Load();
  if (!local_client.is_loaded()) {
    return out;
  }
  auto const local_id = local_client.id();
  out.local_client_obj_id = local_id.id();
  auto const& local_name = local_client->name;
  std::optional<ChatComponent::LocalJoinProbe> name_fallback;
  for (auto const& record : chat->journal) {
    if (!record.event.is_valid() ||
        record.event->GetClassId() != JoinClientEvent::kClassId) {
      continue;
    }
    auto join = JoinClientEvent::ptr{record.event};
    join.Load();
    if (!join.is_loaded() || !join->client.is_valid()) {
      continue;
    }
    if (join->client.id() == local_id) {
      out.kind = ChatComponent::LocalJoinMatchKind::kObjId;
      out.join_client_obj_id = join->client.id().id();
      return out;
    }
    // Name match is migration/debug only — never used for activation.
    if (!name_fallback.has_value()) {
      join->client.Load();
      if (join->client.is_loaded() && !local_name.empty() &&
          join->client->name == local_name) {
        ChatComponent::LocalJoinProbe fb = out;
        fb.kind = ChatComponent::LocalJoinMatchKind::kNameFallback;
        fb.join_client_obj_id = join->client.id().id();
        name_fallback = fb;
      }
    }
  }
  return name_fallback.has_value() ? *name_fallback : out;
}

bool LocalClientHasJoinInJournal(Chat::ptr const& chat,
                                 Client::ptr const& local_client) {
  return ProbeLocalJoinInJournal(chat, local_client).kind ==
         ChatComponent::LocalJoinMatchKind::kObjId;
}

}  // namespace

ChatComponent::ChatComponent(SyncReplica replica, Client::ptr local_client,
                             Chat::ptr chat, SendFunction send,
                             RawSendFunction raw_send, ConnectFunction connect,
                             ChatSyncTiming timing, LogFunction log)
    : local_client_{std::move(local_client)},
      chat_{[&] {
        auto c = std::move(chat);
        assert(c.is_valid());
        c.Load();
        assert(c.is_loaded());
        return c;
      }()},
      peer_set_{[&] {
        auto ps = chat_->peer_set;
        assert(ps.is_valid());
        ps.Load();
        assert(ps.is_loaded());
        return ps;
      }()},
      connect_{std::move(connect)},
      sync_{replica,
            chat_,
            peer_set_,
            std::move(send),
            std::move(raw_send),
            timing,
            [this]() { NotifyPresentationChanged(); },
            std::move(log)} {
  assert(local_client_.is_valid());
  local_client_.Load();
  assert(local_client_.is_loaded());
  assert(replica.shared_root_id == chat_.id());
  // Room client mode may construct before host-created Join exists.
}

ChatComponent::~ChatComponent() { Stop(); }

void ChatComponent::Start() {
  if (running_) {
    return;
  }
  peer_set_.Load();
  assert(peer_set_.is_loaded());
  running_ = true;
  if (connect_) {
    for (auto const& peer : peer_set_->peers) {
      if (!peer.remote_uid.empty()) {
        connect_(peer.remote_uid);
      }
    }
  }
  sync_.Start();
  NotifyPresentationChanged();
}

void ChatComponent::Stop() {
  bool const was_running = running_;
  running_ = false;
  subscribers_.clear();
  if (was_running) {
    sync_.Stop();
  }
}

bool ChatComponent::is_running() const { return running_; }

bool ChatComponent::HasLocalJoin() const {
  return LocalClientHasJoinInJournal(chat_, local_client_);
}

ChatComponent::LocalJoinProbe ChatComponent::ProbeLocalJoin() const {
  return ProbeLocalJoinInJournal(chat_, local_client_);
}

void ChatComponent::SetIncomingPeerAuthorize(
    ChatSyncController::IncomingPeerAuthorizeFunction fn) {
  sync_.SetIncomingPeerAuthorize(std::move(fn));
}

void ChatComponent::SetQueryPeerSchedule(QueryPeerScheduleFunction fn) {
  sync_.SetQueryPeerSchedule(std::move(fn));
}

AddPeerResult ChatComponent::AddPeer(ae::Uid const& remote_uid) {
  if (!running_) {
    return AddPeerResult::kNotRunning;
  }
  if (remote_uid.empty()) {
    return AddPeerResult::kInvalidUid;
  }
  peer_set_.Load();
  assert(peer_set_.is_loaded());
  if (peer_set_->Find(remote_uid) != nullptr ||
      sync_.FindSession(remote_uid) != nullptr) {
    return AddPeerResult::kAlreadyPresent;
  }
  Trace("CHAT_ADD_PEER", ae::Format("peer={}", remote_uid));
  if (connect_) {
    connect_(remote_uid);
  }
  Trace("SYNC_SESSION_CREATE_BEGIN", ae::Format("peer={}", remote_uid));
  sync_.AddPeer(remote_uid);
  Trace("SYNC_SESSION_CREATE_END", ae::Format("peer={}", remote_uid));
  NotifyPresentationChanged();
  return AddPeerResult::kAdded;
}

void ChatComponent::PublishCommittedJournalEvent(EventRecord const& record) {
  if (!running_) {
    return;
  }
  sync_.LocalEventCommitted(chat_, record);
}

std::optional<std::uint32_t> ChatComponent::SubmitText(std::string text) {
  AssertBusinessThread("ChatComponent::SubmitText");
  text = TrimWhitespace(std::move(text));
  if (text.empty() || !running_) {
    return std::nullopt;
  }
  if (!LocalClientHasJoinInJournal(chat_, local_client_)) {
    return std::nullopt;
  }
  assert(chat_.is_valid());
  chat_.Load();
  assert(chat_.is_loaded());
  assert(chat_.domain() != nullptr);
  assert(local_client_.is_valid());
  local_client_.Load();
  assert(local_client_.is_loaded());

  Trace("MESSAGE_EVENT_CREATE_BEGIN", ae::Format("size={}", text.size()));
  auto event = AddMessageEvent::ptr::Create(ae::CreateWith{*chat_.domain()});
  event->author = local_client_;
  event->text = std::move(text);
  auto const event_id = event.id().id();
  Trace("MESSAGE_EVENT_CREATE_END", ae::Format("event_id={}", event_id));

  Trace("MESSAGE_COMMIT_BEGIN", ae::Format("event_id={}", event_id));
  chat_->Commit(event);
  chat_.Save();
  Trace("MESSAGE_COMMIT_END", ae::Format("event_id={}", event_id));

  EventRecord const* committed = nullptr;
  for (auto const& record : chat_->journal) {
    if (record.event.is_valid() && record.event.id().id() == event_id) {
      committed = &record;
      break;
    }
  }
  assert(committed != nullptr);
  sync_.LocalEventCommitted(chat_, *committed);
  Trace("MESSAGE_LOCAL_COMMITTED", ae::Format("event_id={}", event_id));
  NotifyPresentationChanged();
  return event_id;
}

void ChatComponent::Receive(ae::Uid const& remote_uid,
                            std::vector<std::uint8_t> const& bytes) {
  AssertBusinessThread("ChatComponent::Receive");
  if (!running_) {
    return;
  }
  sync_.Receive(remote_uid, bytes);
}

void ChatComponent::FlushUnauthorizedIfAuthorized() {
  if (!running_) {
    return;
  }
  sync_.FlushUnauthorizedIfAuthorized();
}

void ChatComponent::Tick(ae::TimePoint now) {
  if (!running_) {
    return;
  }
  Trace("SYNC_TICK");
  sync_.Tick(now);
}

void ChatComponent::EnsurePresentable() {
  chat_.Load();
  if (!chat_.is_loaded()) {
    return;
  }
  local_client_.Load();
  for (auto const& record : chat_->journal) {
    if (!record.event.is_valid()) {
      continue;
    }
    record.event.Load();
    if (!record.event.is_loaded()) {
      continue;
    }
    auto const class_id = record.event->GetClassId();
    if (class_id == JoinClientEvent::kClassId) {
      auto join = JoinClientEvent::ptr{record.event};
      if (!join.is_loaded()) {
        continue;
      }
      if (join->client.is_valid()) {
        join->client.Load();
      }
    } else if (class_id == AddMessageEvent::kClassId) {
      auto msg = AddMessageEvent::ptr{record.event};
      if (!msg.is_loaded()) {
        continue;
      }
      if (msg->author.is_valid()) {
        msg->author.Load();
      }
    }
  }
}

ChatParticipantView ChatComponent::MakeParticipantView(
    Client::ptr client) const {
  ChatParticipantView view{};
  if (!client.is_valid()) {
    return view;
  }
  view.client_obj_id = client.id().id();
  client.Load();
  if (client.is_loaded()) {
    view.display_name = client->name;
  }
  return view;
}

ChatPresentationSnapshot ChatComponent::CapturePresentation() const {
  ChatPresentationSnapshot snapshot{};
  snapshot.running = running_;
  snapshot.local_participant = MakeParticipantView(local_client_);
  snapshot.local_presence = sync_.GetLocalPresence();

  if (chat_.is_valid()) {
    chat_.Load();
    if (chat_.is_loaded()) {
      snapshot.timeline.reserve(chat_->journal.size());
      for (auto const& record : chat_->journal) {
        if (!record.event.is_valid()) {
          continue;
        }
        auto const class_id = record.event->GetClassId();
        ChatTimelineItemView item{};
        item.event_obj_id = record.event.id().id();
        item.timestamp_us = record.timestamp_us;

        if (class_id == JoinClientEvent::kClassId) {
          auto join = JoinClientEvent::ptr{record.event};
          join.Load();
          if (!join.is_loaded()) {
            continue;
          }
          item.kind = ChatTimelineItemKind::kJoined;
          item.author = MakeParticipantView(join->client);
        } else if (class_id == AddMessageEvent::kClassId) {
          auto msg = AddMessageEvent::ptr{record.event};
          msg.Load();
          if (!msg.is_loaded()) {
            continue;
          }
          item.kind = ChatTimelineItemKind::kMessage;
          item.author = MakeParticipantView(msg->author);
          item.text = msg->text;
          if (msg->author.is_valid() && local_client_.is_valid() &&
              msg->author.id() == local_client_.id()) {
            item.direction = ChatMessageDirection::kLocal;
          } else if (msg->author.is_valid()) {
            item.direction = ChatMessageDirection::kRemote;
          }
        } else {
          continue;
        }
        snapshot.timeline.push_back(std::move(item));
      }
    }
  }

  auto local_presence_as_peer = [&]() {
    switch (snapshot.local_presence) {
      case LocalPresenceStatus::kOnline:
        return PeerPresenceStatus::kOnline;
      case LocalPresenceStatus::kOffline:
        return PeerPresenceStatus::kOffline;
      case LocalPresenceStatus::kConnecting:
        return PeerPresenceStatus::kUnknown;
    }
    return PeerPresenceStatus::kUnknown;
  };

  if (room_participants_) {
    auto const& parts = room_participants_();
    bool local_pushed = false;
    for (auto const& part : parts) {
      auto const uid_text = ae::Format("{}", part.uid);
      bool const is_local =
          part.client_obj_id != 0 &&
          part.client_obj_id == snapshot.local_participant.client_obj_id;
      ChatPeerStatusView status{};
      status.remote_uid = uid_text;
      status.display_name =
          part.display_name.empty() ? uid_text : part.display_name;
      status.is_local = is_local;
      status.is_host = !room_host_uid_.empty() && part.uid == room_host_uid_;
      if (is_local) {
        status.presence = local_presence_as_peer();
        status.online = status.presence == PeerPresenceStatus::kOnline;
        local_pushed = true;
      } else {
        status.presence = sync_.GetPeerPresence(part.uid);
        status.online = status.presence == PeerPresenceStatus::kOnline;
        if (auto const* session = sync_.FindSession(part.uid)) {
          status.initial_sync_complete = session->initial_sync_complete();
          status.pending_packets = session->pending_packet_count();
        }
      }
      snapshot.peers.push_back(std::move(status));
    }
    if (!local_pushed) {
      ChatPeerStatusView you{};
      you.display_name = snapshot.local_participant.display_name;
      you.is_local = true;
      you.is_host = false;
      you.presence = local_presence_as_peer();
      you.online = you.presence == PeerPresenceStatus::kOnline;
      snapshot.peers.insert(snapshot.peers.begin(), std::move(you));
    }
  } else {
    ChatPeerStatusView you{};
    you.display_name = snapshot.local_participant.display_name;
    you.is_local = true;
    you.presence = local_presence_as_peer();
    you.online = you.presence == PeerPresenceStatus::kOnline;
    snapshot.peers.push_back(std::move(you));
    if (peer_set_.is_valid()) {
      peer_set_.Load();
      if (peer_set_.is_loaded()) {
        for (auto const& peer : peer_set_->peers) {
          ChatPeerStatusView status{};
          status.remote_uid = ae::Format("{}", peer.remote_uid);
          status.display_name = status.remote_uid;
          status.is_local = false;
          status.presence = sync_.GetPeerPresence(peer.remote_uid);
          status.online = status.presence == PeerPresenceStatus::kOnline;
          if (auto const* session = sync_.FindSession(peer.remote_uid)) {
            status.initial_sync_complete = session->initial_sync_complete();
            status.pending_packets = session->pending_packet_count();
          }
          snapshot.peers.push_back(std::move(status));
        }
      }
    }
  }

  return snapshot;
}

ChatComponent::SubscriptionId ChatComponent::SubscribePresentationChanged(
    PresentationChangedFunction callback) {
  auto const id = next_subscription_id_++;
  subscribers_.push_back(Subscriber{id, std::move(callback)});
  return id;
}

void ChatComponent::Unsubscribe(SubscriptionId id) {
  subscribers_.erase(
      std::remove_if(subscribers_.begin(), subscribers_.end(),
                     [id](Subscriber const& s) { return s.id == id; }),
      subscribers_.end());
}

void ChatComponent::NotifyPresentationChanged() {
  if (!running_) {
    return;
  }
  std::vector<SubscriptionId> ids;
  ids.reserve(subscribers_.size());
  for (auto const& sub : subscribers_) {
    ids.push_back(sub.id);
  }
  for (auto const id : ids) {
    if (!running_) {
      break;
    }
    PresentationChangedFunction callback;
    for (auto const& sub : subscribers_) {
      if (sub.id == id) {
        callback = sub.callback;
        break;
      }
    }
    if (!callback) {
      continue;
    }
    callback();
    if (!running_) {
      break;
    }
  }
}

}  // namespace apptraverse::chat
