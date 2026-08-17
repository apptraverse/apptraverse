#ifndef APPTRAVERSE_EXAMPLES_ANDROID_CHAT_PRESENTER_H_
#define APPTRAVERSE_EXAMPLES_ANDROID_CHAT_PRESENTER_H_

#include <string>
#include <utility>

#include "aether-miscpp/types/small_function.h"

#include "model/chat_presenter.h"
#include "apptraverse/object_macros.h"

#include "../../common/chat_presentation.h"
#include "../../common/chat_transcript.h"

namespace apptraverse {

class AndroidChatPresenter : public ChatPresenter {
  APPTRAVERSE_OBJECT(AndroidChatPresenter, ChatPresenter, 0)

 protected:
  AndroidChatPresenter() = default;

 public:
  // Publishes an UTF-8 transcript to the Android UI. It is process state, not
  // model state, so it is never reflected and must be set after every load.
  using TranscriptPublisher = ae::SmallFunction<void(std::string const&)>;

  explicit AndroidChatPresenter(ae::ObjProp prop) : ChatPresenter{prop} {}

  AE_OBJECT_REFLECT()

  void SetTranscriptPublisher(TranscriptPublisher publisher) {
    publisher_ = std::move(publisher);
  }

  bool has_transcript_publisher() const { return static_cast<bool>(publisher_); }

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

#endif  // APPTRAVERSE_EXAMPLES_ANDROID_CHAT_PRESENTER_H_
