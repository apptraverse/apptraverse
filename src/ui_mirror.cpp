#include "apptraverse/ui_mirror.h"

#include <cassert>
#include <cstring>

#include "apptraverse/object_serialization.h"

namespace apptraverse {
namespace {

void WriteU32(ByteSink& out, std::uint32_t value) {
  out.write(&value, sizeof(value));
}
void WriteU64(ByteSink& out, std::uint64_t value) {
  out.write(&value, sizeof(value));
}

void ReadU32(ByteSource& in, std::uint32_t* value) {
  in.read(value, sizeof(*value));
}
void ReadU64(ByteSource& in, std::uint64_t* value) {
  in.read(value, sizeof(*value));
}

}  // namespace

UiMirror::UiMirror(ae::Domain& ui_domain, ae::IDomainStorage& ui_storage,
                   PublishNotify notify)
    : ui_domain_{ui_domain},
      ui_storage_{ui_storage},
      notify_{std::move(notify)} {}

PublicationChannel<3>& UiMirror::ChannelFor(std::uint32_t root_id) {
  auto& channel = channels_[root_id];
  if (!channel) {
    channel = std::make_unique<PublicationChannel<3>>();
  }
  return *channel;
}

std::uint64_t UiMirror::publication_count(std::uint32_t root_id) const {
  auto it = channels_.find(root_id);
  if (it == channels_.end() || !it->second) {
    return 0;
  }
  return it->second->publish_count();
}

bool UiMirror::Publish(std::uint32_t root_id,
                       std::vector<Node*> const& changed) {
  if (changed.empty()) {
    return true;
  }
  auto& channel = ChannelFor(root_id);
  if (channel.has_unread_published()) {
    return false;
  }

  auto* buffer = channel.AcquireProducer();
  assert(buffer != nullptr);
  ByteSink& out = buffer->sink;
  WriteU32(out, static_cast<std::uint32_t>(changed.size()));

  for (Node* node : changed) {
    WriteU32(out, node->obj_id.id());
    WriteU64(out, node->Generation());
    auto const size_at = out.bytes.size();
    WriteU32(out, 0);
    auto const payload_at = out.bytes.size();
    SerializeObjectGraphToBuffer(*node, out);
    auto const payload_size =
        static_cast<std::uint32_t>(out.bytes.size() - payload_at);
    std::memcpy(out.bytes.data() + size_at, &payload_size, sizeof(payload_size));
  }

  channel.NotePublished();
  channel.PublishProducer();
  if (notify_) {
    notify_(root_id, &channel);
  }
  return true;
}

UiApplyResult UiMirror::ApplyPublished(PublicationChannel<3>& channel,
                                       std::uint32_t root_id) {
  UiApplyResult result;
  result.root_id = root_id;
  auto* buffer = channel.TakePublished();
  if (buffer == nullptr) {
    return result;
  }

  ByteSource in;
  in.data = buffer->sink.bytes.data();
  in.size = buffer->sink.bytes.size();

  std::uint32_t record_count = 0;
  ReadU32(in, &record_count);
  assert(in.ok);

  for (std::uint32_t i = 0; i < record_count; ++i) {
    std::uint32_t obj_id = 0;
    std::uint64_t generation = 0;
    std::uint32_t payload_size = 0;
    ReadU32(in, &obj_id);
    ReadU64(in, &generation);
    ReadU32(in, &payload_size);
    assert(in.ok);
    assert(in.pos + payload_size <= in.size);

    auto object = ui_domain_.Find(ae::ObjId{obj_id});
    assert(object);

    ByteSource payload;
    payload.data = in.data + in.pos;
    payload.size = payload_size;
    DeserializeObjectGraphFromBuffer(*object, payload, ui_domain_, ui_storage_);
    in.pos += payload_size;
    FinalizeUiNodeState(*object, generation);
    result.changed_obj_ids.push_back(obj_id);
  }

  channel.ReleaseConsumer();
  return result;
}

}  // namespace apptraverse
