#include "ui_runtime_registry.h"

#include <cassert>
#include <vector>

#include "demo_model.h"

namespace apptraverse {
namespace {

ae::seri::BinaryArchive<ae::seri::BinaryVectorBuffer<>> PayloadArchive(
    ByteSource const& in, std::vector<std::uint8_t>& bytes) {
  bytes.assign(in.data, in.data + in.size);
  return UiArchive(bytes);
}

}  // namespace

RuntimeObject* UiRuntimeRegistry::FindOrCreate(std::uint32_t obj_id,
                                               std::uint32_t class_id) {
  if (auto* existing = Find(obj_id)) {
    assert(existing->class_id == class_id);
    return existing;
  }
  std::unique_ptr<RuntimeObject> created;
  if (class_id == Window::kClassId) {
    created = std::make_unique<RuntimeWindow>();
  } else if (class_id == TextToolbar::kClassId) {
    created = std::make_unique<RuntimeTextToolbar>();
  } else if (class_id == ColorToolbar::kClassId) {
    created = std::make_unique<RuntimeColorToolbar>();
  } else if (class_id == Chat::kClassId) {
    created = std::make_unique<RuntimeChat>();
  } else {
    assert(false && "unsupported runtime class");
    return nullptr;
  }
  created->obj_id = obj_id;
  created->class_id = class_id;
  auto* raw = created.get();
  by_id_.emplace(obj_id, std::move(created));
  return raw;
}

void ReadRuntimeWindow(RuntimeWindow& obj, ByteSource& in) {
  std::vector<std::uint8_t> bytes;
  auto archive = PayloadArchive(in, bytes);
  archive.Load(obj.left);
  archive.Load(obj.top);
  archive.Load(obj.right);
  archive.Load(obj.bottom);
  archive.Load(obj.dpi);
  archive.Load(obj.client_width);
  archive.Load(obj.client_height);
}

void ReadRuntimeTextToolbar(RuntimeTextToolbar& obj, ByteSource& in) {
  std::vector<std::uint8_t> bytes;
  auto archive = PayloadArchive(in, bytes);
  std::uint32_t text_id = 0;
  archive.Load(obj.x);
  archive.Load(obj.y);
  archive.Load(obj.width);
  archive.Load(obj.height);
  archive.Load(text_id);
  obj.text_id = ae::ObjId{text_id};
}

void ReadRuntimeColorToolbar(RuntimeColorToolbar& obj, ByteSource& in) {
  std::vector<std::uint8_t> bytes;
  auto archive = PayloadArchive(in, bytes);
  archive.Load(obj.x);
  archive.Load(obj.y);
  archive.Load(obj.width);
  archive.Load(obj.height);
  archive.Load(obj.color);
}

void ReadRuntimeChat(RuntimeChat& obj, ByteSource& in) {
  std::vector<std::uint8_t> bytes;
  auto archive = PayloadArchive(in, bytes);
  archive.Load(obj.x);
  archive.Load(obj.y);
  archive.Load(obj.width);
  archive.Load(obj.height);
  archive.Load(obj.messages);
}

}  // namespace apptraverse
