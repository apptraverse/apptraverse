#ifndef APPTRAVERSE_EXAMPLES_APPLE_CHAT_PRESENTER_H_
#define APPTRAVERSE_EXAMPLES_APPLE_CHAT_PRESENTER_H_

#include <string>
#include <utility>

#include "aether-miscpp/types/small_function.h"

#include "model/chat_presenter.h"
#include "apptraverse/object_macros.h"

#include "../../common/chat_presentation.h"
#include "../../common/chat_transcript.h"

namespace apptraverse {

class AppleChatPresenter : public ChatPresenter {
  APPTRAVERSE_OBJECT(AppleChatPresenter, ChatPresenter, 0)

 protected:
  AppleChatPresenter() = default;

 public:
  using TranscriptPublisher = ae::SmallFunction<void(std::string const&)>;

  explicit AppleChatPresenter(ae::ObjProp prop) : ChatPresenter{prop} {}

  AE_OBJECT_REFLECT()

  void SetTranscriptPublisher(TranscriptPublisher publisher) {
    publisher_ = std::move(publisher);
  }

  void PublishTranscriptText(std::string const& utf8) {
    if (!publisher_) {
      return;
    }
    publisher_(utf8);
  }

  void PublishPresentation(chat::ChatPresentationSnapshot const& snapshot) {
    PublishTranscriptText(examples::FormatChatPresentationUtf8(snapshot));
  }

 private:
  TranscriptPublisher publisher_;
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_EXAMPLES_APPLE_CHAT_PRESENTER_H_
