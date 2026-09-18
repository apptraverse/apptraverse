#include "messenger_model.h"

#include "apptraverse/model_object_proxy.h"
#include "apptraverse/object_macros.h"

namespace apptraverse {
namespace {

APPTRAVERSE_REGISTER(Surface);
APPTRAVERSE_REGISTER(SurfacePresenter);
APPTRAVERSE_REGISTER(Surfaces);
APPTRAVERSE_REGISTER(Dialog);
APPTRAVERSE_REGISTER(SurfaceBoundsChangedEvent);
APPTRAVERSE_REGISTER(SurfacePresentationSizeChangedEvent);
APPTRAVERSE_REGISTER(OwnUidChangedEvent);
APPTRAVERSE_REGISTER(PeerUidChangedEvent);
APPTRAVERSE_REGISTER(DraftChangedEvent);
APPTRAVERSE_REGISTER(MessageAppendedEvent);
APPTRAVERSE_REGISTER(Application);

void ArchiveActiveConversation(Dialog& dialog) {
  if (dialog.peer_uid.empty() && dialog.draft.empty() &&
      dialog.messages.empty()) {
    return;
  }
  for (auto& entry : dialog.archived) {
    if (entry.peer_uid == dialog.peer_uid) {
      entry.draft = dialog.draft;
      entry.messages = dialog.messages;
      return;
    }
  }
  dialog.archived.push_back(
      PeerConversation{dialog.peer_uid, dialog.draft, dialog.messages});
}

void RestoreConversation(Dialog& dialog, std::string const& peer_uid) {
  dialog.peer_uid = peer_uid;
  dialog.draft.clear();
  dialog.messages.clear();
  for (auto const& entry : dialog.archived) {
    if (entry.peer_uid == peer_uid) {
      dialog.draft = entry.draft;
      dialog.messages = entry.messages;
      return;
    }
  }
}

}  // namespace

void EnsureMessengerModelRegistration() {
  (void)&g_apptraverse_registrar_Application;
  (void)&g_apptraverse_registrar_Surface;
  (void)&g_apptraverse_registrar_Dialog;
  (void)&g_apptraverse_registrar_SurfaceBoundsChangedEvent;
  (void)&g_apptraverse_registrar_SurfacePresentationSizeChangedEvent;
  (void)&g_apptraverse_registrar_OwnUidChangedEvent;
  (void)&g_apptraverse_registrar_PeerUidChangedEvent;
  (void)&g_apptraverse_registrar_DraftChangedEvent;
  (void)&g_apptraverse_registrar_MessageAppendedEvent;
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

void Dialog::AppendOutgoingMessage(std::string text) {
  if (text.empty() || peer_uid.empty()) {
    return;
  }
  auto event = MessageAppendedEvent::ptr::Create(ae::CreateWith{*domain});
  event->text = std::move(text);
  event->outgoing = true;
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

void SurfacePresenter::PresentationSizeChanged(std::int32_t width,
                                               std::int32_t height) {
  model_proxy->Invoke<Surface>(surface->obj_id, &Surface::SetPresentationSize,
                               width, height);
}

void SurfacePresenter::PeerUidEntered(std::string uid) {
  model_proxy->Invoke<Dialog>(surface->dialog->obj_id, &Dialog::SetPeerUid,
                              std::move(uid));
}

void SurfacePresenter::DraftEdited(std::string text) {
  model_proxy->Invoke<Dialog>(surface->dialog->obj_id, &Dialog::SetDraft,
                              std::move(text));
}

void SurfacePresenter::SendDraft(std::string text) {
  model_proxy->Invoke<Dialog>(surface->dialog->obj_id,
                              &Dialog::AppendOutgoingMessage, std::move(text));
}

}  // namespace apptraverse
