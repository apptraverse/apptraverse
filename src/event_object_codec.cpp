#include "apptraverse/event_object_codec.h"

#include <cassert>
#include <cstring>
#include <map>
#include <memory>
#include <optional>
#include <utility>

#include "aether/clock.h"
#include "aether/obj/registry.h"

namespace apptraverse {
namespace {

class BlobDomainStorage final : public ae::IDomainStorage {
 public:
  using VersionData = std::map<std::uint8_t, ae::ObjectData>;
  using ClassData = std::map<std::uint32_t, VersionData>;
  using ObjClassData = std::map<ae::ObjId, std::optional<ClassData>>;

  class Writer final : public ae::IDomainStorageWriter {
   public:
    Writer(ae::DomainQuery query, BlobDomainStorage& storage)
        : query_{query}, storage_{&storage} {}

    ~Writer() override {
      storage_->Save(query_, std::move(buffer_));
    }

    void write(void const* data, std::size_t size) override {
      auto const* bytes = static_cast<std::uint8_t const*>(data);
      buffer_.insert(buffer_.end(), bytes, bytes + size);
    }

   private:
    ae::DomainQuery query_;
    BlobDomainStorage* storage_;
    ae::ObjectData buffer_;
  };

  class Reader final : public ae::IDomainStorageReader {
   public:
    explicit Reader(ae::ObjectData const& data) : data_{&data} {}

    void read(void* data, std::size_t size) override {
      assert(offset_ + size <= data_->size());
      std::memcpy(data, data_->data() + offset_, size);
      offset_ += size;
    }

    ae::ReadResult result() const override { return ae::ReadResult::kYes; }
    void result(ae::ReadResult) override {}

   private:
    ae::ObjectData const* data_;
    std::size_t offset_{0};
  };

  std::unique_ptr<ae::IDomainStorageWriter> Store(
      ae::DomainQuery const& query) override {
    return std::make_unique<Writer>(query, *this);
  }

  ae::ClassList Enumerate(ae::ObjId const& obj_id) override {
    auto it = state_.find(obj_id);
    if (it == state_.end() || !it->second) {
      return {};
    }
    ae::ClassList classes;
    for (auto const& [class_id, _] : *it->second) {
      classes.push_back(class_id);
    }
    return classes;
  }

  ae::DomainLoad Load(ae::DomainQuery const& query) override {
    auto obj_it = state_.find(query.id);
    if (obj_it == state_.end()) {
      return {ae::DomainLoadResult::kEmpty, {}};
    }
    if (!obj_it->second) {
      return {ae::DomainLoadResult::kRemoved, {}};
    }
    auto class_it = obj_it->second->find(query.class_id);
    if (class_it == obj_it->second->end()) {
      return {ae::DomainLoadResult::kEmpty, {}};
    }
    auto version_it = class_it->second.find(query.version);
    if (version_it == class_it->second.end()) {
      return {ae::DomainLoadResult::kEmpty, {}};
    }
    return {ae::DomainLoadResult::kLoaded,
            std::make_unique<Reader>(version_it->second)};
  }

  void Remove(ae::ObjId const& obj_id) override {
    state_[obj_id].reset();
  }

  void CleanUp() override { state_.clear(); }

  void Save(ae::DomainQuery const& query, ae::ObjectData data) {
    auto& classes = state_[query.id];
    if (!classes) {
      classes.emplace();
    }
    (*classes)[query.class_id][query.version] = std::move(data);
  }

  ObjClassData const& state() const { return state_; }

  void Import(EventObjectPayload const& payload, ae::ObjId obj_id) {
    for (auto const& layer : payload.layers) {
      Save(ae::DomainQuery{obj_id, layer.class_id, layer.version}, layer.data);
    }
  }

 private:
  ObjClassData state_;
};

}  // namespace

EventObjectPayload EncodeEventObject(Event::ptr const& event) {
  assert(event && event.is_valid());
  event.Load();
  assert(event);

  BlobDomainStorage capture;
  ae::Domain capture_domain{ae::Now(), capture};
  ae::DomainGraph graph{&capture_domain};

  ae::ObjId const transfer_id{1};
  ae::Ptr<ae::Obj> as_obj = event.Load();
  assert(as_obj);

  auto* factory =
      ae::Registry::GetRegistry().FindFactory(as_obj->GetClassId());
  assert(factory != nullptr);
  assert(factory->save != nullptr);
  factory->save(&graph, as_obj, transfer_id);

  EventObjectPayload payload;
  auto const& obj_state = capture.state().at(transfer_id);
  assert(obj_state);
  for (auto const& [class_id, versions] : *obj_state) {
    payload.class_ids.push_back(class_id);
    for (auto const& [version, data] : versions) {
      payload.layers.push_back(
          EventObjectPayload::Layer{class_id, version, data});
    }
  }
  return payload;
}

Event::ptr DecodeEventObject(ae::Domain& destination_domain,
                             ae::IDomainStorage& destination_storage,
                             EventObjectPayload const& payload,
                             ae::ObjId destination_event_id) {
  assert(destination_event_id.IsValid());
  assert(!payload.layers.empty());

  for (auto const& layer : payload.layers) {
    auto writer = destination_storage.Store(
        ae::DomainQuery{destination_event_id, layer.class_id, layer.version});
    assert(writer);
    if (!layer.data.empty()) {
      writer->write(layer.data.data(), layer.data.size());
    }
  }

  auto event = Event::ptr::Declare(
      ae::CreateWith{destination_domain}.with_id(destination_event_id));
  event.Load();
  assert(event);
  return event;
}

}  // namespace apptraverse
