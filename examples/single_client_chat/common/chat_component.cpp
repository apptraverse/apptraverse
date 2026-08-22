#include "chat_component.h"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <optional>
#include <utility>

#include "aether-miscpp/format/format.h"

#include "model/chat_events.h"
#include "model/chat_presenter.h"

namespace apptraverse::chat {
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
    : storage_{&replica.storage},
      local_client_{std::move(local_client)},
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
  assert(storage_ != nullptr);
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

void ChatComponent::SetReconnectPeer(
    ChatSyncController::ReconnectPeerFunction fn) {
  sync_.SetReconnectPeer(std::move(fn));
}

void ChatComponent::SetQueryPeerSchedule(QueryPeerScheduleFunction fn) {
  sync_.SetQueryPeerSchedule(std::move(fn));
}

PeerReachability ChatComponent::GetPeerReachability(
    ae::Uid const& remote_uid) const {
  return sync_.GetPeerReachability(remote_uid);
}

bool ChatComponent::IsPeerOfflineMissedVisit(
    ae::Uid const& remote_uid) const {
  return sync_.IsPeerOfflineMissedVisit(remote_uid);
}

bool ChatComponent::IsPeerOfflineNoFuturePing(
    ae::Uid const& remote_uid) const {
  return sync_.IsPeerOfflineNoFuturePing(remote_uid);
}

bool ChatComponent::ShowOfflinePingMarker(ae::Uid const& remote_uid) const {
  return sync_.ShowOfflinePingMarker(remote_uid);
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
  if (connect_) {
    connect_(remote_uid);
  }
  sync_.AddPeer(remote_uid);
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

  auto event = AddMessageEvent::ptr::Create(ae::CreateWith{*chat_.domain()});
  event->author = local_client_;
  event->text = std::move(text);
  auto const event_id = event.id().id();

  chat_->Commit(event);
  chat_.Save();

  EventRecord const* committed = nullptr;
  for (auto const& record : chat_->journal) {
    if (record.event.is_valid() && record.event.id().id() == event_id) {
      committed = &record;
      break;
    }
  }
  assert(committed != nullptr);
  // Immediate sync publish before presentation (outbound enqueue first).
  sync_.LocalEventCommitted(chat_, *committed);

  NotifyPresentationChanged();
  return event_id;
}

void ChatComponent::Receive(ae::Uid const& remote_uid,
                            std::vector<std::uint8_t> const& bytes) {
  if (!running_) {
    return;
  }
  sync_.Receive(remote_uid, bytes);
}

void ChatComponent::NotifyTransportSessionReady(
    ae::Uid const& remote_uid, std::uint64_t transport_generation) {
  if (!running_) {
    return;
  }
  sync_.NotifyTransportSessionReady(remote_uid, transport_generation);
}

void ChatComponent::Tick(ae::TimePoint now) {
  if (!running_) {
    return;
  }
  sync_.Tick(now);
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

  if (peer_set_.is_valid()) {
    peer_set_.Load();
    if (peer_set_.is_loaded()) {
      snapshot.peers.reserve(peer_set_->peers.size());
      for (auto const& peer : peer_set_->peers) {
        ChatPeerStatusView status{};
        status.remote_uid = ae::Format("{}", peer.remote_uid);
        status.online = sync_.IsPeerOnline(peer.remote_uid);
        status.offline_missed_visit =
            sync_.ShowOfflinePingMarker(peer.remote_uid);
        if (auto const* session = sync_.FindSession(peer.remote_uid)) {
          status.initial_sync_complete = session->initial_sync_complete();
          status.pending_packets = session->pending_packet_count();
        }
        snapshot.peers.push_back(std::move(status));
      }
    }
  }

  bool show_marker = false;
  for (auto const& peer : snapshot.peers) {
    if (peer.offline_missed_visit && peer.pending_packets > 0) {
      show_marker = true;
      break;
    }
  }
  if (show_marker) {
    for (auto& item : snapshot.timeline) {
      if (item.kind == ChatTimelineItemKind::kMessage &&
          item.direction == ChatMessageDirection::kLocal) {
        item.show_offline_marker = true;
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