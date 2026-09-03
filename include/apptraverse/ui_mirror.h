#ifndef APPTRAVERSE_UI_MIRROR_H_
#define APPTRAVERSE_UI_MIRROR_H_

#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

#include "aether-objects/obj/domain.h"
#include "aether-objects/obj/idomain_storage.h"

#include "apptraverse/node.h"
#include "apptraverse/publication_channel.h"

namespace apptraverse {

struct UiApplyResult {
  std::uint32_t root_id{0};
  std::vector<std::uint32_t> changed_obj_ids;
};

class UiMirror {
 public:
  using PublishNotify =
      std::function<void(std::uint32_t root_id, PublicationChannel<3>*)>;

  UiMirror(ae::Domain& ui_domain, ae::IDomainStorage& ui_storage,
           PublishNotify notify);

  ae::Domain& ui_domain() { return ui_domain_; }

  // Returns false when the channel still has an unread publication.
  bool Publish(std::uint32_t root_id, std::vector<Node*> const& changed);
  UiApplyResult ApplyPublished(PublicationChannel<3>& channel,
                               std::uint32_t root_id);

  PublicationChannel<3>& ChannelFor(std::uint32_t root_id);
  std::uint64_t publication_count(std::uint32_t root_id) const;

 private:
  ae::Domain& ui_domain_;
  ae::IDomainStorage& ui_storage_;
  PublishNotify notify_;
  std::unordered_map<std::uint32_t, std::unique_ptr<PublicationChannel<3>>>
      channels_;
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_UI_MIRROR_H_
