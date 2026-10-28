#include "apptraverse/ui_mirror.h"

#include <cassert>
#include <vector>

#include "aether-miscpp/reflect/reflect.h"
#include "aether-miscpp/serialization/binary_archive.h"
#include "aether-objects/obj/registry.h"

#include "apptraverse/node.h"
#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/object_serialization.h"

namespace apptraverse {
namespace {

struct UiMirrorPublicationRecord {
  std::uint32_t object_id;
  std::uint32_t class_id;
  std::uint64_t generation;
  std::vector<std::uint8_t> graph_bytes;
  AE_REFLECT_MEMBERS(object_id, class_id, generation, graph_bytes)
};

struct PublishedRecord {
  std::uint32_t obj_id{0};
  std::uint32_t class_id{0};
  std::uint64_t generation{0};
  std::vector<std::uint8_t> graph_bytes;
  bool applied{false};
};

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
  if (buffer == nullptr) {
    WriteFatalStderr("fatal: ui mirror publication channel exhausted\n");
    std::abort();
  }
  std::vector<UiMirrorPublicationRecord> wire_records;
  wire_records.reserve(changed.size());
  for (Node* node : changed) {
    UiMirrorPublicationRecord rec;
    rec.object_id = node->obj_id.id();
    rec.class_id = node->GetClassId();
    rec.generation = node->Generation();
    ByteSink payload;
    SerializeObjectGraphToBuffer(*node, payload);
    rec.graph_bytes = std::move(payload.bytes);
    wire_records.push_back(std::move(rec));
  }

  ByteSink& out = buffer->sink;
  ae::seri::BinaryVectorBuffer archive_buffer{out.bytes};
  ae::seri::BinaryArchive archive{std::move(archive_buffer)};
  if (!archive.Save(wire_records).IsOk()) {
    WriteFatalStderr("fatal: ui mirror publication save failed\n");
    std::abort();
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

  std::vector<std::uint8_t> copy = buffer->sink.bytes;
  ae::seri::BinaryVectorBuffer archive_buffer{copy};
  ae::seri::BinaryArchive archive{std::move(archive_buffer)};
  std::vector<UiMirrorPublicationRecord> wire_records;
  if (archive.Load(wire_records).IsErr()) {
    WriteFatalStderr("fatal: ui mirror publication load failed\n");
    std::abort();
  }

  std::vector<PublishedRecord> records;
  records.reserve(wire_records.size());
  for (UiMirrorPublicationRecord const& wire : wire_records) {
    PublishedRecord rec;
    rec.obj_id = wire.object_id;
    rec.class_id = wire.class_id;
    rec.generation = wire.generation;
    rec.graph_bytes = wire.graph_bytes;
    records.push_back(std::move(rec));
  }

  auto apply_existing = [&](PublishedRecord& rec) -> bool {
    if (rec.applied) {
      return false;
    }
    auto object = ui_domain_.Find(ae::ObjId{rec.obj_id});
    if (!object) {
      return false;
    }
    ByteSource payload;
    payload.data = rec.graph_bytes.data();
    payload.size = rec.graph_bytes.size();
    payload.pos = 0;
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
    auto created =
        CreateUiShell(ui_domain_, ae::ObjId{rec.obj_id}, rec.class_id);
    if (!created) {
      WriteFatalStderr("fatal: ui mirror shell create failed\n");
      std::abort();
    }
    keepalive.push_back(created);
    if (!apply_existing(rec)) {
      WriteFatalStderr("fatal: ui mirror published record apply failed\n");
      std::abort();
    }
  }

  channel.ReleaseConsumer();
  return result;
}

}  // namespace apptraverse
