#include "ui_publication.h"

#include <cassert>
#include <string>

#include "demo_log.h"

namespace apptraverse {
namespace {

void FillObject(UiSubgraphObject* slot, Node& node,
                std::unordered_map<std::uint32_t, std::uint64_t> const& last,
                void (*write_state)(void const*, ByteSink&)) {
  auto const id = node.obj_id.id();
  slot->obj_id = id;
  slot->class_id = node.GetClassId();
  slot->generation = node.Generation();
  auto it = last.find(id);
  slot->last_published_generation = it == last.end() ? 0 : it->second;
  slot->model = &node;
  slot->write_state = write_state;
}

}  // namespace

void SerializeWindowRoot(Window& root, ImmutableObjectStore const& store,
                         std::unordered_map<std::uint32_t, std::uint64_t>&
                             last_published_generation,
                         ByteSink& out) {
  (void)store;
  root.EnsureCurrentGeneration();
  UiSubgraphObject objects[4];
  std::size_t count = 0;
  FillObject(&objects[count++], root, last_published_generation,
             &Window::WriteUiState);

  if (root.text_toolbar) {
    root.text_toolbar.Load();
    root.text_toolbar->EnsureCurrentGeneration();
    FillObject(&objects[count++], *root.text_toolbar, last_published_generation,
               &TextToolbar::WriteUiState);
  }
  if (root.color_toolbar) {
    root.color_toolbar.Load();
    root.color_toolbar->EnsureCurrentGeneration();
    FillObject(&objects[count++], *root.color_toolbar,
               last_published_generation, &ColorToolbar::WriteUiState);
  }
  if (root.chat) {
    root.chat.Load();
    root.chat->EnsureCurrentGeneration();
    FillObject(&objects[count++], *root.chat, last_published_generation,
               &Chat::WriteUiState);
  }

  UiConstRefObject consts[1];
  std::size_t const_count = 0;
  if (root.text_toolbar) {
    consts[0].const_object_id = root.text_toolbar->text_id.id();
    consts[0].class_id = ImmutableString::kClassId;
    const_count = 1;
  }

  SerializeUiSubgraphToBuffer(root.obj_id.id(), objects, count, consts,
                              const_count, out);

  for (std::size_t i = 0; i < count; ++i) {
    last_published_generation[objects[i].obj_id] = objects[i].generation;
  }

  demo::DemoLog("pub root=" + std::to_string(root.obj_id.id()) +
                " records=" + std::to_string(count + const_count));
}

UiApplyResult DeserializeUiSubgraphIntoExisting(
    ByteSink const& buffer, UiRuntimeRegistry& registry,
    ImmutableObjectStore const& store) {
  UiApplyResult result;
  ByteSource in;
  in.data = buffer.bytes.data();
  in.size = buffer.bytes.size();
  std::uint32_t record_count = 0;
  if (!ReadUiSubgraphHeader(in, &result.root_id, &record_count)) {
    assert(false && "invalid UI subgraph header");
    return result;
  }
  ByteSource whole = in;
  whole.pos = 0;
  whole.data = buffer.bytes.data();
  whole.size = buffer.bytes.size();

  for (std::uint32_t i = 0; i < record_count; ++i) {
    UiSubgraphRecord record;
    if (!ReadUiSubgraphRecord(in, whole, &record)) {
      assert(false && "invalid UI subgraph record");
      return result;
    }
    if (record.kind == UiRecordKind::kConstRef) {
      auto const* immutable = store.Find(record.obj_id);
      assert(immutable != nullptr);
      (void)immutable;
      result.const_ref_ids.push_back(record.obj_id);
      continue;
    }
    if (record.kind == UiRecordKind::kReuseObject) {
      auto* existing = registry.Find(record.obj_id);
      assert(existing != nullptr);
      assert(existing->generation == record.generation);
      result.reused_obj_ids.push_back(record.obj_id);
      continue;
    }
    assert(record.kind == UiRecordKind::kObjectState);
    auto* runtime = registry.FindOrCreate(record.obj_id, record.class_id);
    if (record.class_id == Window::kClassId) {
      ReadRuntimeWindow(*static_cast<RuntimeWindow*>(runtime), record.payload);
    } else if (record.class_id == TextToolbar::kClassId) {
      ReadRuntimeTextToolbar(*static_cast<RuntimeTextToolbar*>(runtime),
                             record.payload);
    } else if (record.class_id == ColorToolbar::kClassId) {
      ReadRuntimeColorToolbar(*static_cast<RuntimeColorToolbar*>(runtime),
                              record.payload);
    } else if (record.class_id == Chat::kClassId) {
      ReadRuntimeChat(*static_cast<RuntimeChat*>(runtime), record.payload);
    } else {
      assert(false && "unsupported UI class");
    }
    runtime->generation = record.generation;
    result.changed_obj_ids.push_back(record.obj_id);
  }
  return result;
}

}  // namespace apptraverse
