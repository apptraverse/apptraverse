#include "chat_component.h"

#include <algorithm>
#include <cassert>
#include <cctype>
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

bool LocalClientHasJoinInJournal(Chat::ptr const& chat,
                                 Client::ptr const& local_client) {
  if (!chat.is_loaded() || !local_client.is_valid()) {
    return false;
  }
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
    if (join->client.id() == local_client.id()) {
      return true;
    }
  }
  return false;
}

}  // namespace

ChatComponent::ChatComponent(SyncReplica replica, Client::ptr local_client,
                             Chat::ptr chat, SendFunction send,
                             RawSendFunction raw_send, ConnectFunction connect,
                             ChatSyncTiming timing, bool auto_accept_incoming,
                             LogFunction log)
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
            auto_accept_incoming,
            [this]() { NotifyPresentationChanged(); },
            std::move(log)} {
  assert(local_client_.is_valid());
  local_client_.Load();
  assert(local_client_.is_loaded());
  assert(replica.shared_root_id == chat_.id());
  assert(LocalClientHasJoinInJournal(chat_, local_client_));
}

ChatComponent::~ChatComponent() { Stop(); }

void ChatComponent::Start() {
  if (running_) {
    return;
  }
  running_ = true;
  sync_.Start();
  peer_set_.Load();
  if (peer_set_.is_loaded() && connect_) {
    for (auto const& peer : peer_set_->peers) {
      if (!peer.remote_uid.empty()) {
        connect_(peer.remote_uid);
      }
    }
  }
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
  sync_.AddPeer(remote_uid);
  if (connect_) {
    connect_(remote_uid);
  }
  NotifyPresentationChanged();
  return AddPeerResult::kAdded;
}

std::optional<std::uint32_t> ChatComponent::SubmitText(std::string text) {
  text = TrimWhitespace(std::move(text));
  if (text.empty() || !running_) {
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
        if (auto const* session = sync_.FindSession(peer.remote_uid)) {
          status.initial_sync_complete = session->initial_sync_complete();
          status.pending_packets = session->pending_packet_count();
        }
        snapshot.peers.push_back(std::move(status));
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