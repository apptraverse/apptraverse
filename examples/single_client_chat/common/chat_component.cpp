#include "chat_component.h"

#include <algorithm>
#include <cctype>
#include <utility>

#include "aether-miscpp/format/format.h"

#include "model/chat_entry.h"

namespace apptraverse::examples {
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

}  // namespace

ChatComponent::ChatComponent(SyncReplica replica, Client::ptr local_client,
                             Chat::ptr chat, ChatPresenter::ptr presenter,
                             ChatPeerSet::ptr peer_set, SendFunction send,
                             RawSendFunction raw_send, ChatSyncTiming timing,
                             bool auto_accept_incoming, LogFunction log)
    : local_client_{std::move(local_client)},
      chat_{std::move(chat)},
      presenter_{std::move(presenter)},
      peer_set_{std::move(peer_set)},
      sync_{replica,
            chat_,
            peer_set_,
            std::move(send),
            std::move(raw_send),
            timing,
            auto_accept_incoming,
            [this]() { NotifyPresentationChanged(); },
            std::move(log)} {}

ChatComponent::~ChatComponent() { Stop(); }

void ChatComponent::Start() {
  {
    std::lock_guard lock{mutex_};
    if (running_) {
      return;
    }
    running_ = true;
  }
  sync_.Start();
  NotifyPresentationChanged();
}

void ChatComponent::Stop() {
  bool was_running = false;
  {
    std::lock_guard lock{mutex_};
    generation_.fetch_add(1, std::memory_order_acq_rel);
    subscribers_.clear();
    was_running = running_;
    running_ = false;
  }
  if (was_running) {
    sync_.Stop();
  }
}

bool ChatComponent::is_running() const {
  std::lock_guard lock{mutex_};
  return running_;
}

std::optional<std::reference_wrapper<SharedGraphSyncSession>>
ChatComponent::AddPeer(ae::Uid const& remote_uid) {
  {
    std::lock_guard lock{mutex_};
    if (!running_) {
      return std::nullopt;
    }
  }
  auto& session = sync_.AddPeer(remote_uid);
  NotifyPresentationChanged();
  return session;
}

SharedGraphSyncSession* ChatComponent::FindSession(ae::Uid const& remote_uid) {
  return sync_.FindSession(remote_uid);
}

SharedGraphSyncSession const* ChatComponent::FindSession(
    ae::Uid const& remote_uid) const {
  return sync_.FindSession(remote_uid);
}

std::size_t ChatComponent::runtime_session_count() const {
  return sync_.runtime_session_count();
}

bool ChatComponent::IsPeerOnline(ae::Uid const& remote_uid) const {
  return sync_.IsPeerOnline(remote_uid);
}

void ChatComponent::Receive(ae::Uid const& remote_uid,
                            std::vector<std::uint8_t> const& bytes) {
  {
    std::lock_guard lock{mutex_};
    if (!running_) {
      return;
    }
  }
  sync_.Receive(remote_uid, bytes);
}

void ChatComponent::Tick(ae::TimePoint now) {
  {
    std::lock_guard lock{mutex_};
    if (!running_) {
      return;
    }
  }
  sync_.Tick(now);
}

bool ChatComponent::SubmitText(std::string text) {
  text = TrimWhitespace(std::move(text));
  if (text.empty()) {
    return false;
  }
  {
    std::lock_guard lock{mutex_};
    if (!running_) {
      return false;
    }
  }
  if (!presenter_.is_valid()) {
    return false;
  }
  presenter_.Load();
  if (!presenter_.is_loaded()) {
    return false;
  }
  presenter_->SubmitText(std::move(text));
  NotifyPresentationChanged();
  return true;
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
  {
    std::lock_guard lock{mutex_};
    snapshot.running = running_;
  }

  snapshot.local_participant = MakeParticipantView(local_client_);

  if (chat_.is_valid()) {
    chat_.Load();
    if (chat_.is_loaded()) {
      snapshot.timeline.reserve(chat_->entries.size());
      for (std::size_t i = 0; i < chat_->entries.size(); ++i) {
        auto const& entry = chat_->entries[i];
        ChatTimelineItemView item{};
        item.timeline_index = i;
        item.author = MakeParticipantView(entry.client);
        item.text = entry.text;
        if (entry.kind == ChatEntryKind::kJoined) {
          item.kind = ChatTimelineItemKind::kJoined;
        } else {
          item.kind = ChatTimelineItemKind::kMessage;
          if (entry.client.is_valid() && local_client_.is_valid() &&
              entry.client.id() == local_client_.id()) {
            item.direction = ChatMessageDirection::kLocal;
          } else if (entry.client.is_valid()) {
            item.direction = ChatMessageDirection::kRemote;
          }
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
  std::lock_guard lock{mutex_};
  auto const id = next_subscription_id_++;
  subscribers_.push_back(Subscriber{id, std::move(callback)});
  return id;
}

void ChatComponent::Unsubscribe(SubscriptionId id) {
  std::lock_guard lock{mutex_};
  subscribers_.erase(
      std::remove_if(subscribers_.begin(), subscribers_.end(),
                     [id](Subscriber const& s) { return s.id == id; }),
      subscribers_.end());
}

void ChatComponent::NotifyPresentationChanged() {
  auto const gen = generation_.load(std::memory_order_acquire);
  std::vector<PresentationChangedFunction> callbacks;
  {
    std::lock_guard lock{mutex_};
    if (!running_) {
      return;
    }
    callbacks.reserve(subscribers_.size());
    for (auto const& sub : subscribers_) {
      if (sub.callback) {
        callbacks.push_back(sub.callback);
      }
    }
  }
  if (generation_.load(std::memory_order_acquire) != gen) {
    return;
  }
  for (auto const& callback : callbacks) {
    callback();
  }
}

}  // namespace apptraverse::examples