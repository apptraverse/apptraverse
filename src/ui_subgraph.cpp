#include "apptraverse/ui_subgraph.h"

#include <cassert>
#include <cstring>

namespace apptraverse {
namespace {

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

}  // namespace

void SerializeUiSubgraphToBuffer(std::uint32_t root_id,
                                 UiSubgraphObject const* objects,
                                 std::size_t object_count,
                                 UiConstRefObject const* consts,
                                 std::size_t const_count, ByteSink& out) {
  assert(objects != nullptr || object_count == 0);
  assert(consts != nullptr || const_count == 0);

  auto const record_count = static_cast<std::uint32_t>(object_count + const_count);
  WriteU32(out, kUiSubgraphMagic);
  WriteU32(out, root_id);
  WriteU32(out, record_count);

  for (std::size_t i = 0; i < object_count; ++i) {
    auto const& obj = objects[i];
    bool const changed = obj.last_published_generation != obj.generation;
    if (changed) {
      assert(obj.write_state != nullptr);
      WriteU8(out, static_cast<std::uint8_t>(UiRecordKind::kObjectState));
      WriteU32(out, obj.obj_id);
      WriteU32(out, obj.class_id);
      WriteU64(out, obj.generation);
      auto const size_at = out.bytes.size();
      WriteU32(out, 0);
      auto const payload_at = out.bytes.size();
      obj.write_state(obj.model, out);
      auto const payload_size =
          static_cast<std::uint32_t>(out.bytes.size() - payload_at);
      std::memcpy(out.bytes.data() + size_at, &payload_size,
                  sizeof(payload_size));
    } else {
      WriteU8(out, static_cast<std::uint8_t>(UiRecordKind::kReuseObject));
      WriteU32(out, obj.obj_id);
      WriteU32(out, obj.class_id);
      WriteU64(out, obj.generation);
    }
  }

  for (std::size_t i = 0; i < const_count; ++i) {
    WriteU8(out, static_cast<std::uint8_t>(UiRecordKind::kConstRef));
    WriteU32(out, consts[i].const_object_id);
    WriteU32(out, consts[i].class_id);
  }
}

bool ReadUiSubgraphHeader(ByteSource& in, std::uint32_t* root_id,
                          std::uint32_t* record_count) {
  std::uint32_t magic = 0;
  if (!ReadU32(in, &magic) || magic != kUiSubgraphMagic) {
    return false;
  }
  return ReadU32(in, root_id) && ReadU32(in, record_count);
}

bool ReadUiSubgraphRecord(ByteSource& in, ByteSource const& whole,
                          UiSubgraphRecord* record) {
  std::uint8_t kind = 0;
  if (!ReadU8(in, &kind)) {
    return false;
  }
  record->kind = static_cast<UiRecordKind>(kind);
  if (!ReadU32(in, &record->obj_id) || !ReadU32(in, &record->class_id)) {
    return false;
  }
  record->generation = 0;
  record->payload = {};
  if (record->kind == UiRecordKind::kConstRef) {
    return true;
  }
  if (!ReadU64(in, &record->generation)) {
    return false;
  }
  if (record->kind == UiRecordKind::kReuseObject) {
    return true;
  }
  if (record->kind != UiRecordKind::kObjectState) {
    return false;
  }
  std::uint32_t payload_size = 0;
  if (!ReadU32(in, &payload_size)) {
    return false;
  }
  if (in.pos + payload_size > in.size) {
    return false;
  }
  record->payload.data = whole.data + in.pos;
  record->payload.size = payload_size;
  record->payload.pos = 0;
  in.pos += payload_size;
  return true;
}

}  // namespace apptraverse
