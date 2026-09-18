#include "messenger_model.h"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "aether-objects/obj/registry.h"

#include "apptraverse/model_object_proxy.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/runtime_node.h"
#include "apptraverse/shared_sync_runtime.h"

#include "aether_frame_endpoint.h"
#include "aether_link.h"
#include "messenger_aether_uid.h"
#include "messenger_dial.h"
#include "messenger_ids.h"

namespace apptraverse {
namespace {

APPTRAVERSE_REGISTER(Surface);
APPTRAVERSE_REGISTER(SurfacePresenter);
APPTRAVERSE_REGISTER(Surfaces);
APPTRAVERSE_REGISTER(Dialog);
APPTRAVERSE_REGISTER(Conversation);
APPTRAVERSE_REGISTER(SurfaceBoundsChangedEvent);
APPTRAVERSE_REGISTER(SurfacePresentationSizeChangedEvent);
APPTRAVERSE_REGISTER(OwnUidChangedEvent);
APPTRAVERSE_REGISTER(PeerUidChangedEvent);
APPTRAVERSE_REGISTER(DraftChangedEvent);
APPTRAVERSE_REGISTER(MessageAppendedEvent);
APPTRAVERSE_REGISTER(ConversationBoundEvent);
APPTRAVERSE_REGISTER(LocalEndpointBoundEvent);
APPTRAVERSE_REGISTER(MessageSequenceReservedEvent);
APPTRAVERSE_REGISTER(MessageAddedEvent);
APPTRAVERSE_REGISTER(Application);

void LogMessenger(char const* stage, std::string const& detail) {
  std::fprintf(stderr, "[messenger] %s %s\n", stage, detail.c_str());
  std::fflush(stderr);
}

std::vector<MessengerMessage> ActiveMessagesForArchive(Dialog const& dialog) {
  if (dialog.conversation.is_valid()) {
    std::vector<MessengerMessage> out;
    out.reserve(dialog.conversation->messages.size());
    for (auto const& message : dialog.conversation->messages) {
      out.push_back(MessengerMessage{
          message.text, message.id.origin_uid == dialog.own_uid});
    }
    return out;
  }
  return dialog.messages;
}

void ArchiveActiveConversation(Dialog& dialog) {
  auto const messages = ActiveMessagesForArchive(dialog);
  if (dialog.peer_uid.empty() && dialog.draft.empty() && messages.empty()) {
    return;
  }
  for (auto& entry : dialog.archived) {
    if (entry.peer_uid == dialog.peer_uid) {
      entry.draft = dialog.draft;
      entry.messages = messages;
      return;
    }
  }
  dialog.archived.push_back(
      PeerConversation{dialog.peer_uid, dialog.draft, messages});
}

void RestoreConversation(Dialog& dialog, std::string const& peer_uid) {
  dialog.peer_uid = peer_uid;
  dialog.draft.clear();
  dialog.messages.clear();
  dialog.conversation = {};
  for (auto const& entry : dialog.archived) {
    if (entry.peer_uid == peer_uid) {
      dialog.draft = entry.draft;
      dialog.messages = entry.messages;
      return;
    }
  }
}

ae::ObjId RemoteShareIdForPeer(Conversation const& conversation,
                               std::string const& peer_uid) {
  for (auto const& share : conversation.shares) {
    if (share.link.is_valid() && share.link->EndpointUid() == peer_uid) {
      return share.share_id;
    }
  }
  return {};
}

bool ConversationMatchesPeerPair(Conversation const& conversation,
                                 std::string const& own_uid,
                                 std::string const& peer_uid) {
  bool saw_own = false;
  bool saw_peer = false;
  for (auto const& share : conversation.shares) {
    if (!share.link.is_valid()) {
      continue;
    }
    auto const& endpoint = share.link->EndpointUid();
    if (endpoint == own_uid) {
      saw_own = true;
    } else if (endpoint == peer_uid) {
      saw_peer = true;
    }
  }
  return saw_own && saw_peer;
}

}  // namespace

void EnsureMessengerModelRegistration() {
  example::chat_demo::EnsureAetherLinkRegistration();
  (void)&g_apptraverse_registrar_Application;
  (void)&g_apptraverse_registrar_Surface;
  (void)&g_apptraverse_registrar_Dialog;
  (void)&g_apptraverse_registrar_Conversation;
  (void)&g_apptraverse_registrar_SurfaceBoundsChangedEvent;
  (void)&g_apptraverse_registrar_SurfacePresentationSizeChangedEvent;
  (void)&g_apptraverse_registrar_OwnUidChangedEvent;
  (void)&g_apptraverse_registrar_PeerUidChangedEvent;
  (void)&g_apptraverse_registrar_DraftChangedEvent;
  (void)&g_apptraverse_registrar_MessageAppendedEvent;
  (void)&g_apptraverse_registrar_ConversationBoundEvent;
  (void)&g_apptraverse_registrar_LocalEndpointBoundEvent;
  (void)&g_apptraverse_registrar_MessageSequenceReservedEvent;
  (void)&g_apptraverse_registrar_MessageAddedEvent;
}

void Surface::Apply(SurfaceBoundsChangedEvent const& event) {
  desktop_x = event.x;
  desktop_y = event.y;
  desktop_width = event.width;
  desktop_height = event.height;
  NoteMaterializedChange();
}

void Surface::Apply(SurfacePresentationSizeChangedEvent const& event) {
  presentation_width = event.width;
  presentation_height = event.height;
  NoteMaterializedChange();
}

void Surface::SetDesktopBounds(std::int32_t x, std::int32_t y,
                               std::int32_t width, std::int32_t height) {
  if (desktop_x == x && desktop_y == y && desktop_width == width &&
      desktop_height == height) {
    return;
  }
  auto event =
      SurfaceBoundsChangedEvent::ptr::Create(ae::CreateWith{*domain});
  event->x = x;
  event->y = y;
  event->width = width;
  event->height = height;
  Commit(event);
}

void Surface::SetPresentationSize(std::int32_t width, std::int32_t height) {
  if (presentation_width == width && presentation_height == height) {
    return;
  }
  auto event = SurfacePresentationSizeChangedEvent::ptr::Create(
      ae::CreateWith{*domain});
  event->width = width;
  event->height = height;
  Commit(event);
}

bool Conversation::CanApply(MessageAddedEvent const& event) const {
  if (event.message.id.origin_uid.empty() ||
      event.message.id.origin_sequence == 0 ||
      event.message.timestamp_us == 0 || event.message.text.empty()) {
    return false;
  }
  return true;
}

void Conversation::Apply(MessageAddedEvent const& event) {
  messages.push_back(event.message);
  NoteMaterializedChange();
}

void Dialog::SetOwnUid(std::string uid) {
  if (own_uid == uid) {
    return;
  }
  auto event = OwnUidChangedEvent::ptr::Create(ae::CreateWith{*domain});
  event->uid = std::move(uid);
  Commit(event);
}

void Dialog::SetPeerUid(std::string uid) {
  if (peer_uid == uid) {
    return;
  }
  auto event = PeerUidChangedEvent::ptr::Create(ae::CreateWith{*domain});
  event->uid = std::move(uid);
  Commit(event);
}

void Dialog::SetDraft(std::string text) {
  if (draft == text) {
    return;
  }
  auto event = DraftChangedEvent::ptr::Create(ae::CreateWith{*domain});
  event->text = std::move(text);
  Commit(event);
}

void Dialog::BindConversation(Conversation::ptr next) {
  if (conversation.is_valid() && next.is_valid() &&
      conversation.id() == next.id()) {
    return;
  }
  if (!conversation.is_valid() && !next.is_valid()) {
    return;
  }
  auto event = ConversationBoundEvent::ptr::Create(ae::CreateWith{*domain});
  event->conversation = std::move(next);
  Commit(event);
}

void Dialog::Apply(OwnUidChangedEvent const& event) {
  own_uid = event.uid;
  NoteMaterializedChange();
}

void Dialog::Apply(PeerUidChangedEvent const& event) {
  ArchiveActiveConversation(*this);
  RestoreConversation(*this, event.uid);
  NoteMaterializedChange();
}

void Dialog::Apply(DraftChangedEvent const& event) {
  draft = event.text;
  NoteMaterializedChange();
}

void Dialog::Apply(MessageAppendedEvent const& event) {
  messages.push_back(MessengerMessage{event.text, event.outgoing});
  if (event.outgoing) {
    draft.clear();
  }
  NoteMaterializedChange();
}

void Dialog::Apply(ConversationBoundEvent const& event) {
  conversation = event.conversation;
  if (conversation.is_valid()) {
    messages.clear();
    for (auto const& message : conversation->messages) {
      messages.push_back(MessengerMessage{
          message.text, message.id.origin_uid == own_uid});
    }
  }
  NoteMaterializedChange();
}

void SurfacePresenter::PresentationSizeChanged(std::int32_t width,
                                               std::int32_t height) {
  model_proxy->Invoke<Surface>(surface->obj_id, &Surface::SetPresentationSize,
                               width, height);
}

void SurfacePresenter::PeerUidEntered(std::string uid) {
  model_proxy->Invoke<Application>(
      ae::ObjId{messenger::ToObjId(messenger::ObjId::Application)},
      &Application::ConfirmPeerUid, std::move(uid));
}

void SurfacePresenter::DraftEdited(std::string text) {
  model_proxy->Invoke<Dialog>(surface->dialog->obj_id, &Dialog::SetDraft,
                              std::move(text));
}

void SurfacePresenter::SendDraft(std::string text) {
  model_proxy->Invoke<Application>(
      ae::ObjId{messenger::ToObjId(messenger::ObjId::Application)},
      &Application::AppendOutgoingMessage, std::move(text));
}

void Application::Apply(LocalEndpointBoundEvent const& event) {
  local_endpoint_uid = event.endpoint_uid;
  NoteMaterializedChange();
}

bool Application::CanApply(MessageSequenceReservedEvent const& event) const {
  return event.reserved_sequence == next_message_sequence &&
         event.reserved_sequence != 0 &&
         event.reserved_sequence !=
             std::numeric_limits<std::uint64_t>::max();
}

void Application::Apply(MessageSequenceReservedEvent const& event) {
  next_message_sequence = event.reserved_sequence + 1;
  NoteMaterializedChange();
}

void Application::ConfirmPeerUid(std::string raw) {
  std::string canonical;
  if (!TryCanonicalizeAetherUid(raw, canonical)) {
    LogMessenger("dial_reject", "invalid_peer_uid");
    return;
  }
  Dialog& dialog = *surfaces->surfaces.front()->dialog;
  if (dialog.peer_uid == canonical) {
    SetupActivePeerSync();
    MaybeRetryDial();
    return;
  }
  if (!dialog.peer_uid.empty()) {
    TeardownPeer(dialog.peer_uid);
  }
  peer_prepared_for_sync = false;
  last_dial_send = {};
  dialog.SetPeerUid(canonical);
  LogMessenger("dial_local_peer",
               "own=" + local_endpoint_uid + " peer=" + canonical);
  SetupActivePeerSync();
  MaybeRetryDial();
}

void Application::OnAetherLocalUid(std::string uid) {
  if (local_endpoint_uid != uid) {
    auto event =
        LocalEndpointBoundEvent::ptr::Create(ae::CreateWith{*domain});
    event->endpoint_uid = uid;
    Commit(event);
  }
  surfaces->surfaces.front()->dialog->SetOwnUid(std::move(uid));
}

void Application::OnAetherReady() {
  aether_ready = true;
}

void Application::SendDialControl(bool ack) {
  Dialog& dialog = *surfaces->surfaces.front()->dialog;
  if (aether == nullptr || dialog.peer_uid.empty()) {
    return;
  }
  std::vector<std::uint8_t> bytes;
  messenger_dial::Encode(
      ack ? messenger_dial::Kind::DialAck : messenger_dial::Kind::DialRequest,
      bytes);
  aether->SendControl(dialog.peer_uid, std::move(bytes));
  LogMessenger(ack ? "dial_ack_tx" : "dial_req_tx",
               "to=" + dialog.peer_uid + " own=" + local_endpoint_uid);
}

void Application::MaybeRetryDial() {
  Dialog& dialog = *surfaces->surfaces.front()->dialog;
  if (!aether_ready || aether == nullptr || dialog.peer_uid.empty() ||
      local_endpoint_uid.empty()) {
    return;
  }
  // Stop dialing once both sides have a bound conversation.
  if (dialog.conversation.is_valid() && peer_prepared_for_sync) {
    return;
  }
  auto const now = std::chrono::steady_clock::now();
  if (last_dial_send.time_since_epoch().count() != 0 &&
      now - last_dial_send < std::chrono::milliseconds(500)) {
    return;
  }
  last_dial_send = now;
  aether->OpenPeer(dialog.peer_uid);
  SendDialControl(false);
}

void Application::OnControlMessage(std::string source_uid,
                                   std::vector<std::uint8_t> bytes) {
  messenger_dial::Kind kind{};
  if (!messenger_dial::Decode(bytes, kind)) {
    LogMessenger("dial_ignore", "malformed_control from=" + source_uid);
    return;
  }
  if (source_uid.empty() || local_endpoint_uid.empty()) {
    return;
  }
  if (source_uid == local_endpoint_uid) {
    return;
  }

  Dialog& dialog = *surfaces->surfaces.front()->dialog;
  if (!dialog.peer_uid.empty() && dialog.peer_uid != source_uid) {
    LogMessenger("dial_reject",
                 "busy peer=" + dialog.peer_uid + " from=" + source_uid);
    return;
  }

  if (dialog.peer_uid.empty()) {
    peer_prepared_for_sync = false;
    last_dial_send = {};
    dialog.SetPeerUid(source_uid);
    LogMessenger("dial_accept_peer",
                 "own=" + local_endpoint_uid + " peer=" + source_uid);
  }

  if (kind == messenger_dial::Kind::DialRequest ||
      kind == messenger_dial::Kind::DialAck) {
    peer_prepared_for_sync = true;
  }

  LogMessenger(kind == messenger_dial::Kind::DialAck ? "dial_ack_rx"
                                                     : "dial_req_rx",
               "from=" + source_uid + " own=" + local_endpoint_uid);
  SetupActivePeerSync();
  if (kind == messenger_dial::Kind::DialRequest) {
    SendDialControl(true);
  }
}

void Application::AppendOutgoingMessage(std::string text) {
  Dialog& dialog = *surfaces->surfaces.front()->dialog;
  if (text.empty() || !dialog.conversation.is_valid() ||
      dialog.peer_uid.empty() || local_endpoint_uid.empty() ||
      next_message_sequence == 0 ||
      next_message_sequence == std::numeric_limits<std::uint64_t>::max()) {
    LogMessenger("send_blocked",
                 dialog.conversation.is_valid() ? "preconditions"
                                                : "no_conversation");
    return;
  }

  Conversation& conversation = *dialog.conversation;
  auto now_us = static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count());
  for (auto it = conversation.messages.rbegin();
       it != conversation.messages.rend(); ++it) {
    if (it->id.origin_uid == local_endpoint_uid) {
      if (now_us <= it->timestamp_us) {
        now_us = it->timestamp_us + 1;
      }
      break;
    }
  }

  SharedEventId const identity{
      .origin_uid = local_endpoint_uid,
      .origin_sequence = next_message_sequence,
  };

  auto res_event =
      MessageSequenceReservedEvent::ptr::Create(ae::CreateWith{*domain});
  res_event->reserved_sequence = identity.origin_sequence;
  Commit(res_event);

  auto msg_event =
      MessageAddedEvent::ptr::Create(ae::CreateWith{*conversation.domain});
  msg_event->message = MessageValue{
      .id = identity,
      .timestamp_us = now_us,
      .text = std::move(text),
  };
  conversation.CommitShared(msg_event, identity,
                            SharedEventOrder{.timestamp_us = now_us});

  dialog.SetDraft("");
  LogMessenger("send_local", "seq=" + std::to_string(identity.origin_sequence) +
                                 " conv=" +
                                 std::to_string(conversation.obj_id.id()));
}

void Application::TeardownPeer(std::string const& peer_uid) {
  peer_prepared_for_sync = false;
  last_dial_send = {};
  if (sync_runtime != nullptr) {
    sync_runtime->ForgetInitialNodeFromEndpoint(peer_uid);
  }
  if (aether != nullptr) {
    aether->ClosePeer(peer_uid);
  }
}

void Application::SetupActivePeerSync() {
  if (!aether_ready || aether == nullptr || sync_runtime == nullptr) {
    return;
  }
  Dialog& dialog = *surfaces->surfaces.front()->dialog;
  if (dialog.peer_uid.empty() || local_endpoint_uid.empty()) {
    return;
  }

  aether->OpenPeer(dialog.peer_uid);

  if (dialog.conversation.is_valid()) {
    Conversation& conversation = *dialog.conversation;
    if (!ConversationMatchesPeerPair(conversation, local_endpoint_uid,
                                     dialog.peer_uid)) {
      LogMessenger("restore_reject",
                   "conversation_pair_mismatch peer=" + dialog.peer_uid);
      return;
    }
    conversation.SetJournalCompactionBlocked(true);
    if (!conversation.HasMaterializedChangeNotifier()) {
      conversation.CopyMaterializedChangeNotifierFrom(dialog);
    }
    if (!sync_runtime->FindNode(conversation.obj_id).is_valid()) {
      sync_runtime->RegisterNode(dialog.conversation);
      LogMessenger("restore_register",
                   "conv=" + std::to_string(conversation.obj_id.id()) +
                       " peer=" + dialog.peer_uid);
    }
    return;
  }

  if (local_endpoint_uid < dialog.peer_uid) {
    auto conversation =
        Conversation::ptr::Create(ae::CreateWith{*domain});
    InitializeRuntimeNode(*conversation, dialog);
    conversation->SetJournalCompactionBlocked(true);

    auto local_link = example::chat_demo::AetherLink::ptr::Create(
        ae::CreateWith{*domain});
    local_link->endpoint_uid = local_endpoint_uid;
    InitializeRuntimeNode(*local_link, dialog);

    auto remote_link = example::chat_demo::AetherLink::ptr::Create(
        ae::CreateWith{*domain});
    remote_link->endpoint_uid = dialog.peer_uid;
    InitializeRuntimeNode(*remote_link, dialog);

    conversation->AddShare(local_link, ShareAccess::ReadWrite);
    conversation->AddShare(remote_link, ShareAccess::ReadWrite);
    dialog.BindConversation(conversation);

    if (!sync_runtime->FindNode(conversation.id()).is_valid()) {
      sync_runtime->RegisterNode(conversation);
    }
    LogMessenger("journal_create",
                 "conv=" + std::to_string(conversation.id().id()) +
                     " peer=" + dialog.peer_uid);

    if (peer_prepared_for_sync) {
      ae::ObjId const remote_share =
          RemoteShareIdForPeer(*conversation, dialog.peer_uid);
      if (remote_share.is_valid()) {
        sync_runtime->SyncInitialState(conversation.id(), remote_share);
        LogMessenger("initial_sync_tx",
                     "conv=" + std::to_string(conversation.id().id()));
      }
    }
  } else {
    sync_runtime->ExpectInitialNodeFromEndpoint(dialog.peer_uid,
                                                Conversation::kClassId);
    LogMessenger("journal_expect", "from=" + dialog.peer_uid);
  }
}

void Application::DriveConversationSync() {
  if (sync_runtime == nullptr) {
    return;
  }
  Dialog& dialog = *surfaces->surfaces.front()->dialog;
  MaybeRetryDial();
  if (!dialog.conversation.is_valid() || dialog.peer_uid.empty()) {
    return;
  }

  Conversation& conversation = *dialog.conversation;
  if (!sync_runtime->FindNode(conversation.obj_id).is_valid()) {
    assert(false &&
           "Conversation must be registered before DriveConversationSync");
    LogMessenger("drive_invariant", "unregistered_conversation");
    return;
  }

  ae::ObjId const remote_share =
      RemoteShareIdForPeer(conversation, dialog.peer_uid);
  if (!remote_share.is_valid()) {
    return;
  }

  auto const sync_index =
      conversation.FindLinkSyncIndexForShare(remote_share);
  if (sync_index >= conversation.link_sync_states.size()) {
    return;
  }
  auto state = conversation.link_sync_states[sync_index];
  if (!state.is_valid()) {
    return;
  }
  if (!state.is_loaded()) {
    state.Load();
  }

  auto const phase = state->GetInitialSyncPhase();
  if (phase == InitialSyncPhase::NotStarted) {
    if (!peer_prepared_for_sync) {
      return;
    }
    sync_runtime->SyncInitialState(conversation.obj_id, remote_share);
  } else if (phase == InitialSyncPhase::Pending) {
    sync_runtime->SyncInitialState(conversation.obj_id, remote_share);
  } else if (phase == InitialSyncPhase::Complete) {
    sync_runtime->SyncNextEvent(conversation.obj_id, remote_share);
  }
}

bool Application::NeedsPeriodicSyncWake() const {
  if (!aether_ready || sync_runtime == nullptr) {
    return false;
  }
  Dialog const& dialog = *surfaces->surfaces.front()->dialog;
  if (dialog.peer_uid.empty() || local_endpoint_uid.empty()) {
    return false;
  }
  return true;
}

}  // namespace apptraverse
