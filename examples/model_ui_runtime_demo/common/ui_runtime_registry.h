#ifndef APPTRAVERSE_UI_RUNTIME_REGISTRY_H_
#define APPTRAVERSE_UI_RUNTIME_REGISTRY_H_

#include <cassert>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "aether/obj/obj_id.h"

#include "apptraverse/ui_subgraph.h"

namespace apptraverse {

struct RuntimeObject {
  virtual ~RuntimeObject() = default;
  std::uint32_t obj_id{0};
  std::uint32_t class_id{0};
  std::uint64_t generation{0};
};

struct RuntimeWindow : RuntimeObject {
  std::int32_t left{0};
  std::int32_t top{0};
  std::int32_t right{0};
  std::int32_t bottom{0};
  std::int32_t dpi{96};
  std::int32_t client_width{0};
  std::int32_t client_height{0};
};

struct RuntimeTextToolbar : RuntimeObject {
  std::int32_t x{0};
  std::int32_t y{0};
  std::int32_t width{0};
  std::int32_t height{0};
  ae::ObjId text_id;
};

struct RuntimeColorToolbar : RuntimeObject {
  std::int32_t x{0};
  std::int32_t y{0};
  std::int32_t width{0};
  std::int32_t height{0};
  std::uint32_t color{0};
};

struct RuntimeChat : RuntimeObject {
  std::int32_t x{0};
  std::int32_t y{0};
  std::int32_t width{0};
  std::int32_t height{0};
  std::vector<std::string> messages;
};

class UiRuntimeRegistry {
 public:
  RuntimeObject* Find(std::uint32_t obj_id) const {
    auto it = by_id_.find(obj_id);
    if (it == by_id_.end()) {
      return nullptr;
    }
    return it->second.get();
  }

  RuntimeObject* FindOrCreate(std::uint32_t obj_id, std::uint32_t class_id);

  template <typename T>
  T* Must(std::uint32_t obj_id) const {
    auto* obj = Find(obj_id);
    assert(obj != nullptr);
    return static_cast<T*>(obj);
  }

 private:
  std::unordered_map<std::uint32_t, std::unique_ptr<RuntimeObject>> by_id_;
};

void ReadRuntimeWindow(RuntimeWindow& obj, ByteSource& in);
void ReadRuntimeTextToolbar(RuntimeTextToolbar& obj, ByteSource& in);
void ReadRuntimeColorToolbar(RuntimeColorToolbar& obj, ByteSource& in);
void ReadRuntimeChat(RuntimeChat& obj, ByteSource& in);

}  // namespace apptraverse

#endif  // APPTRAVERSE_UI_RUNTIME_REGISTRY_H_
