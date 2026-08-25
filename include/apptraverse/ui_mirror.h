#ifndef APPTRAVERSE_UI_MIRROR_H_
#define APPTRAVERSE_UI_MIRROR_H_

#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

#include "aether/obj/domain.h"
#include "aether/obj/obj.h"
#include "aether/ptr/ptr.h"

#include "apptraverse/node.h"
#include "apptraverse/publication_channel.h"

namespace apptraverse {

enum class UiRecordKind : std::uint8_t {
  kObjectState = 1,
  kReuseObject = 2,
};

struct UiApplyResult {
  std::uint32_t root_id{0};
  std::vector<std::uint32_t> changed_obj_ids;
  std::vector<std::uint32_t> reused_obj_ids;
};

class UiMirror {
 public:
  using PublishNotify =
      std::function<void(std::uint32_t root_id, PublicationChannel<3>*)>;

  UiMirror(ae::Domain& ui_domain, PublishNotify notify);

  ae::Domain& ui_domain() { return ui_domain_; }

  void Publish(ae::Obj& model_root);
  UiApplyResult ApplyPublished(PublicationChannel<3>& channel);

  PublicationChannel<3>& ChannelFor(std::uint32_t root_id);
  std::uint64_t publication_count(std::uint32_t root_id) const;

 private:
  ae::Domain& ui_domain_;
  PublishNotify notify_;
  std::unordered_map<std::uint32_t, std::unique_ptr<PublicationChannel<3>>>
      channels_;
  std::unordered_map<std::uint32_t, std::uint64_t> last_published_generation_;
  std::unordered_map<std::uint32_t, ae::Ptr<ae::Obj>> ui_objects_;
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_UI_MIRROR_H_
