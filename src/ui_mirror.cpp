#include "apptraverse/ui_mirror.h"

#include <cassert>
#include <cstring>
#include <vector>

#include "aether-objects/obj/registry.h"

#include "apptraverse/node.h"
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

struct PublishedRecord {
  std::uint32_t obj_id{0};
  std::uint64_t generation{0};
  ByteSource payload;
  bool applied{false};
};

std::uint32_t PreferredClassIdFor(ByteSource in, std::uint32_t want_id) {
  std::uint32_t layer_count = 0;
  in.read(&layer_count, sizeof(layer_count));
  std::uint32_t first = 0;
  std::uint32_t derived = 0;
  for (std::uint32_t i = 0; i < layer_count && in.ok; ++i) {
    std::uint32_t oid = 0;
    std::uint32_t class_id = 0;
    std::uint8_t version = 0;
    std::uint32_t size = 0;
    in.read(&oid, sizeof(oid));
    in.read(&class_id, sizeof(class_id));
    in.read(&version, sizeof(version));
    in.read(&size, sizeof(size));
    if (!in.ok || in.pos + size > in.size) {
      break;
    }
    in.pos += size;
    if (oid != want_id) {
      continue;
    }
    if (first == 0) {
      first = class_id;
    }
    if (class_id != Node::kClassId) {
      derived = class_id;
    }
  }
  return derived != 0 ? derived : first;
}

ae::Ptr<ae::Obj> CreateUiShell(ae::Domain& ui_domain, ae::ObjId id,
                               std::uint32_t class_id) {
  if (auto existing = ui_domain.Find(id)) {
    return existing;
  }
  if (class_id == 0) {
    return {};
  }
  auto* factory = ae::Registry::GetRegistry().FindFactory(class_id);
  if (factory == nullptr || factory->create == nullptr) {
    return {};
  }
  ae::Ptr<ae::Obj> raw = factory->create();
  raw->domain = &ui_domain;
  raw->obj_id = id;
  ui_domain.AddObject(id, raw);
  return raw;
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

  std::vector<PublishedRecord> records;
  records.reserve(record_count);
  for (std::uint32_t i = 0; i < record_count; ++i) {
    PublishedRecord rec;
    std::uint32_t payload_size = 0;
    ReadU32(in, &rec.obj_id);
    ReadU64(in, &rec.generation);
    ReadU32(in, &payload_size);
    assert(in.ok);
    assert(in.pos + payload_size <= in.size);
    rec.payload.data = in.data + in.pos;
    rec.payload.size = payload_size;
    in.pos += payload_size;
    records.push_back(rec);
  }

  auto apply_existing = [&](PublishedRecord& rec) -> bool {
    if (rec.applied) {
      return false;
    }
    auto object = ui_domain_.Find(ae::ObjId{rec.obj_id});
    if (!object) {
      return false;
    }
    ByteSource payload = rec.payload;
    DeserializeObjectGraphFromBuffer(*object, payload, ui_domain_,
                                     ui_storage_);
    FinalizeUiNodeState(*object, rec.generation);
    result.changed_obj_ids.push_back(rec.obj_id);
    rec.applied = true;
    return true;
  };

  // Newly created Nodes (local ChatClient after Aether registration) may be
  // published before the UI Domain has a shell. Apply objects that already
  // exist first so a ChatRoom graph load can ConstructObj those children,
  // then create remaining shells from the published class id.
  bool progress = true;
  while (progress) {
    progress = false;
    for (auto& rec : records) {
      if (rec.obj_id == root_id && apply_existing(rec)) {
        progress = true;
      }
    }
    for (auto& rec : records) {
      if (apply_existing(rec)) {
        progress = true;
      }
    }
  }

  std::vector<ae::Ptr<ae::Obj>> keepalive;
  for (auto& rec : records) {
    if (rec.applied) {
      continue;
    }
    std::uint32_t const class_id =
        PreferredClassIdFor(rec.payload, rec.obj_id);
    auto created = CreateUiShell(ui_domain_, ae::ObjId{rec.obj_id}, class_id);
    assert(created);
    keepalive.push_back(created);
    static_cast<void>(apply_existing(rec));
    assert(rec.applied);
  }

  channel.ReleaseConsumer();
  return result;
}

}  // namespace apptraverse
