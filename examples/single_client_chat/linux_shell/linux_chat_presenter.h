#ifndef APPTRAVERSE_EXAMPLES_LINUX_CHAT_PRESENTER_H_
#define APPTRAVERSE_EXAMPLES_LINUX_CHAT_PRESENTER_H_

#include <functional>
#include <string>
#include <utility>

#include "model/chat_presenter.h"
#include "apptraverse/object_macros.h"

#include "chat_presentation.h"
#include "chat_transcript.h"

namespace apptraverse {

class LinuxChatPresenter : public ChatPresenter {
  APPTRAVERSE_OBJECT(LinuxChatPresenter, ChatPresenter, 0)

 protected:
  LinuxChatPresenter() = default;

 public:
  using TranscriptPublisher = std::function<void(std::string const&)>;

  explicit LinuxChatPresenter(ae::ObjProp prop) : ChatPresenter{prop} {}

  AE_OBJECT_REFLECT()

  void SetTranscriptPublisher(TranscriptPublisher publisher) {
    publisher_ = std::move(publisher);
  }

  void PublishPresentation(chat::ChatPresentationSnapshot const& snapshot) {
    if (!publisher_) {
      return;
    }
    publisher_(examples::FormatChatPresentationUtf8(snapshot));
  }

 private:
  TranscriptPublisher publisher_;
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_EXAMPLES_LINUX_CHAT_PRESENTER_H_
