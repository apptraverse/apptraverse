#include "apptraverse/ui_mirror.h"

#include <cassert>
#include <cstring>

#include "apptraverse/graph_walk.h"
#include "apptraverse/materialized_ops.h"
#include "apptraverse/ui_materialized.h"

namespace apptraverse {
namespace {

constexpr std::uint32_t kUiSubgraphMagic = 0x41545549u;  // 'ATUI'

void WriteU8(ByteSink& out, std::uint8_t value) { out.write(&value, 1); }
void WriteU32(ByteSink& out, std::uint32_t value) {
  out.write(&value, sizeof(value));
}
void WriteU64(ByteSink& out, std::uint64_t value) {
  out.write(&value, sizeof(value));
}

bool ReadU8(ByteSource& in, std::uint8_t* value) {
  in.read(value, 1);
  return in.ok;
}
bool ReadU32(ByteSource& in, std::uint32_t* value) {
  in.read(value, sizeof(*value));
  return in.ok;
}
bool ReadU64(ByteSource& in, std::uint64_t* value) {
  in.read(value, sizeof(*value));
  return in.ok;
}

std::uint64_t ObjectGeneration(ae::Obj const& object) {
  if (auto const* node = dynamic_cast<Node const*>(&object)) {
    return node->Generation();
  }
  return 1;
}

}  // namespace

UiMirror::UiMirror(ae::Domain& ui_domain, PublishNotify notify)
    : ui_domain_{ui_domain}, notify_{std::move(notify)} {}

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

void UiMirror::Publish(ae::Obj& model_root) {
  auto& channel = ChannelFor(model_root.obj_id.id());
  if (channel.has_unread_published()) {
    return;
  }
  std::vector<ae::Obj*> objects;
  CollectReachableObjects(model_root, objects);
  bool needs = false;
  for (ae::Obj* object : objects) {
    auto const gen = ObjectGeneration(*object);
    auto it = last_published_generation_.find(object->obj_id.id());
    if (it == last_published_generation_.end() || it->second != gen) {
      needs = true;
      break;
    }
  }
  if (!needs) {
    return;
  }

  auto* buffer = channel.AcquireProducer();
  assert(buffer != nullptr);
  ByteSink& out = buffer->sink;
  WriteU32(out, kUiSubgraphMagic);
  WriteU32(out, model_root.obj_id.id());
  WriteU32(out, static_cast<std::uint32_t>(objects.size()));

  for (ae::Obj* object : objects) {
    auto const id = object->obj_id.id();
    auto const gen = ObjectGeneration(*object);
    auto it = last_published_generation_.find(id);
    bool const changed =
        it == last_published_generation_.end() || it->second != gen;
    if (changed) {
      WriteU8(out, static_cast<std::uint8_t>(UiRecordKind::kObjectState));
      WriteU32(out, id);
      WriteU32(out, object->GetClassId());
      WriteU64(out, gen);
      auto const size_at = out.bytes.size();
      WriteU32(out, 0);
      auto const payload_at = out.bytes.size();
      SerializeMaterializedObject(*object, out);
      auto const payload_size =
          static_cast<std::uint32_t>(out.bytes.size() - payload_at);
      std::memcpy(out.bytes.data() + size_at, &payload_size,
                  sizeof(payload_size));
    } else {
      WriteU8(out, static_cast<std::uint8_t>(UiRecordKind::kReuseObject));
      WriteU32(out, id);
      WriteU32(out, object->GetClassId());
      WriteU64(out, gen);
    }
    last_published_generation_[id] = gen;
  }

  channel.NotePublished();
  channel.PublishProducer();
  if (notify_) {
    notify_(model_root.obj_id.id(), &channel);
  }
}

UiApplyResult UiMirror::ApplyPublished(PublicationChannel<3>& channel) {
  UiApplyResult result;
  auto* buffer = channel.TakePublished();
  if (buffer == nullptr) {
    return result;
  }
  ByteSource in;
  in.data = buffer->sink.bytes.data();
  in.size = buffer->sink.bytes.size();
  std::uint32_t magic = 0;
  std::uint32_t record_count = 0;
  if (!ReadU32(in, &magic) || magic != kUiSubgraphMagic ||
      !ReadU32(in, &result.root_id) || !ReadU32(in, &record_count)) {
    channel.ReleaseConsumer();
    assert(false && "invalid UI subgraph header");
    return result;
  }

  struct Pending {
    UiRecordKind kind{};
    std::uint32_t obj_id{0};
    std::uint32_t class_id{0};
    std::uint64_t generation{0};
    std::uint32_t payload_size{0};
    std::size_t payload_pos{0};
  };
  std::vector<Pending> records;
  records.reserve(record_count);
  for (std::uint32_t i = 0; i < record_count; ++i) {
    Pending rec;
    std::uint8_t kind = 0;
    if (!ReadU8(in, &kind) || !ReadU32(in, &rec.obj_id) ||
        !ReadU32(in, &rec.class_id) || !ReadU64(in, &rec.generation)) {
      channel.ReleaseConsumer();
      assert(false && "invalid UI subgraph record");
      return result;
    }
    rec.kind = static_cast<UiRecordKind>(kind);
    if (rec.kind == UiRecordKind::kObjectState) {
      if (!ReadU32(in, &rec.payload_size) ||
          in.pos + rec.payload_size > in.size) {
        channel.ReleaseConsumer();
        assert(false && "invalid UI object payload");
        return result;
      }
      rec.payload_pos = in.pos;
      in.pos += rec.payload_size;
    }
    records.push_back(rec);
  }

  for (auto const& rec : records) {
    if (rec.kind == UiRecordKind::kObjectState) {
      ui_objects_[rec.obj_id] =
          EnsureUiObject(ui_domain_, ae::ObjId{rec.obj_id}, rec.class_id);
    }
  }

  for (auto const& rec : records) {
    if (rec.kind == UiRecordKind::kReuseObject) {
      auto existing = ui_domain_.Find(ae::ObjId{rec.obj_id});
      assert(existing);
      if (auto* node = dynamic_cast<Node*>(existing.get())) {
        assert(node->Generation() == rec.generation);
      }
      result.reused_obj_ids.push_back(rec.obj_id);
      continue;
    }
    auto object = ui_domain_.Find(ae::ObjId{rec.obj_id});
    assert(object);
    ByteSource payload;
    payload.data = buffer->sink.bytes.data() + rec.payload_pos;
    payload.size = rec.payload_size;
    DeserializeMaterializedObject(*object, payload, ui_domain_);
    if (auto* node = dynamic_cast<Node*>(object.get())) {
      node->AdoptPublishedGeneration(rec.generation);
      node->base = {};
      node->journal.clear();
    }
    result.changed_obj_ids.push_back(rec.obj_id);
  }

  channel.ReleaseConsumer();
  return result;
}

}  // namespace apptraverse
