#ifndef APPTRAVERSE_UI_SUBGRAPH_H_
#define APPTRAVERSE_UI_SUBGRAPH_H_

#include <cstdint>
#include <cstring>
#include <vector>

#include "aether-miscpp/serialization/binary_archive.h"

namespace apptraverse {

// Fast UI publication I/O. Not DomainStorage and not object.Save().
struct ByteSink {
  std::vector<std::uint8_t> bytes;

  void write(void const* data, std::size_t size) {
    auto const* src = static_cast<std::uint8_t const*>(data);
    bytes.insert(bytes.end(), src, src + size);
  }

  void clear_keep_capacity() { bytes.clear(); }
};

struct ByteSource {
  std::uint8_t const* data{nullptr};
  std::size_t size{0};
  std::size_t pos{0};
  bool ok{true};

  void read(void* out, std::size_t n) {
    if (!ok || pos + n > size) {
      ok = false;
      std::memset(out, 0, n);
      return;
    }
    std::memcpy(out, data + pos, n);
    pos += n;
  }
};

inline ae::seri::BinaryArchive<ae::seri::BinaryVectorBuffer<>> UiArchive(
    std::vector<std::uint8_t>& bytes) {
  return ae::seri::BinaryArchive{ae::seri::BinaryVectorBuffer<>{bytes}};
}

inline constexpr std::uint32_t kUiSubgraphMagic = 0x41545549u;  // 'ATUI'

enum class UiRecordKind : std::uint8_t {
  kObjectState = 1,
  kReuseObject = 2,
  kConstRef = 3,
};

struct UiSubgraphObject {
  std::uint32_t obj_id{0};
  std::uint32_t class_id{0};
  std::uint64_t generation{0};
  std::uint64_t last_published_generation{0};
  void const* model{nullptr};
  void (*write_state)(void const* model, ByteSink& payload){nullptr};
};

struct UiConstRefObject {
  std::uint32_t const_object_id{0};
  std::uint32_t class_id{0};
};

void SerializeUiSubgraphToBuffer(std::uint32_t root_id,
                                 UiSubgraphObject const* objects,
                                 std::size_t object_count,
                                 UiConstRefObject const* consts,
                                 std::size_t const_count, ByteSink& out);

struct UiSubgraphRecord {
  UiRecordKind kind{UiRecordKind::kObjectState};
  std::uint32_t obj_id{0};
  std::uint32_t class_id{0};
  std::uint64_t generation{0};
  ByteSource payload{};
};

bool ReadUiSubgraphHeader(ByteSource& in, std::uint32_t* root_id,
                          std::uint32_t* record_count);

bool ReadUiSubgraphRecord(ByteSource& in, ByteSource const& whole,
                          UiSubgraphRecord* record);

}  // namespace apptraverse

#endif  // APPTRAVERSE_UI_SUBGRAPH_H_
