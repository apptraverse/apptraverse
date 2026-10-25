#include "apptraverse/overlay_domain_storage.h"



#include <cstdlib>

#include <set>

#include <utility>

#include <vector>



namespace apptraverse {

namespace {



ae::ObjectData ReadLoadedLayerBytes(ae::DomainLoad load) {

  ae::ObjectData bytes;

  if (load.result != ae::DomainLoadResult::kLoaded || !load.reader) {

    return bytes;

  }

  for (;;) {

    std::uint8_t byte = 0;

    ae::seri::SeriResult const result =

        load.reader->Read(ae::seri::DataReadTag{&byte, 1});

    if (!result) {

      if (result.error().error_code == ae::seri::read_eof.error_code) {

        break;

      }

      std::abort();

    }

    bytes.push_back(byte);

  }

  return bytes;

}



void FailIfWriteError(ae::seri::SeriResult const& result) {

  if (!result) {

    std::abort();

  }

}



class BackingComparedStorageWriter final : public ae::IDomainStorageWriter {

 public:

  BackingComparedStorageWriter(ae::DomainQuery query,

                               ae::IDomainStorage& writable,

                               ae::ObjectData backing_bytes)

      : query_{std::move(query)},

        writable_{writable},

        backing_bytes_{std::move(backing_bytes)},

        new_bytes_{} {}



  ~BackingComparedStorageWriter() override {

    if (failed_) {

      return;

    }

    if (new_bytes_ == backing_bytes_) {

      return;

    }

    std::unique_ptr<ae::IDomainStorageWriter> writer = writable_.Store(query_);

    FailIfWriteError(

        writer->Write(ae::seri::DataWriteTag{new_bytes_.data(), new_bytes_.size()}));

  }



  ae::seri::SeriResult Write(ae::seri::SizeWriteTag data) override {

    auto const u_size = static_cast<std::uint32_t>(data.size);

    return Write(ae::seri::DataTag{u_size});

  }



  ae::seri::SeriResult Write(ae::seri::DataWriteTag data) override {

    if (failed_) {

      return ae::Error{ae::seri::write_error};

    }

    auto const* begin = static_cast<std::uint8_t const*>(data.data);

    new_bytes_.insert(new_bytes_.end(), begin, begin + data.size);

    return ae::Ok{ae::seri::good};

  }



 private:

  ae::DomainQuery query_;

  ae::IDomainStorage& writable_;

  ae::ObjectData backing_bytes_;

  ae::ObjectData new_bytes_;

  bool failed_{false};

};



// Supply-constant layers are never persisted to USER; serialized bytes are not
// compared to BACKING (mutation correctness is a development-time rule).
class SupplyConstantDiscardWriter final : public ae::IDomainStorageWriter {
 public:
  ae::seri::SeriResult Write(ae::seri::SizeWriteTag data) override {
    (void)data;
    return ae::Ok{ae::seri::good};
  }

  ae::seri::SeriResult Write(ae::seri::DataWriteTag data) override {
    (void)data;
    return ae::Ok{ae::seri::good};
  }
};



}  // namespace



OverlayDomainStorage::OverlayDomainStorage(ae::IDomainStorage& backing)

    : writable_overlay_{overlay_}, backing_{backing} {}



OverlayDomainStorage::OverlayDomainStorage(ae::IDomainStorage& writable_overlay,

                                           ae::IDomainStorage& backing)

    : writable_overlay_{writable_overlay}, backing_{backing} {}



OverlayDomainStorage::OverlayDomainStorage(

    ae::IDomainStorage& backing,

    std::set<ae::ObjId> supply_constant_ids)

    : writable_overlay_{overlay_},

      backing_{backing},

      supply_constant_ids_{std::move(supply_constant_ids)} {}



OverlayDomainStorage::OverlayDomainStorage(

    ae::IDomainStorage& writable_overlay, ae::IDomainStorage& backing,

    std::set<ae::ObjId> supply_constant_ids)

    : writable_overlay_{writable_overlay},

      backing_{backing},

      supply_constant_ids_{std::move(supply_constant_ids)} {}



OverlayDomainStorage::~OverlayDomainStorage() = default;



bool OverlayDomainStorage::IsSupplyConstant(ae::ObjId id) const {

  return supply_constant_ids_.find(id) != supply_constant_ids_.end();

}



std::unique_ptr<ae::IDomainStorageWriter> OverlayDomainStorage::Store(

    ae::DomainQuery const& query) {

  if (IsSupplyConstant(query.id)) {
    return std::make_unique<SupplyConstantDiscardWriter>();
  }



  if (query.force_materialize) {

    return writable_overlay_.Store(query);

  }

  ae::DomainLoad overlay_load = writable_overlay_.Load(query);

  ae::DomainLoadResult const overlay_result = overlay_load.result;

  overlay_load = {};

  if (overlay_result != ae::DomainLoadResult::kEmpty) {

    return writable_overlay_.Store(query);

  }



  ae::DomainLoad backing_load = backing_.Load(query);

  if (backing_load.result != ae::DomainLoadResult::kLoaded) {

    return writable_overlay_.Store(query);

  }

  ae::ObjectData const backing_bytes = ReadLoadedLayerBytes(std::move(backing_load));

  return std::make_unique<BackingComparedStorageWriter>(query, writable_overlay_,
                                                        backing_bytes);

}



ae::ClassList OverlayDomainStorage::Enumerate(ae::ObjId const& obj_id) {

  if (IsSupplyConstant(obj_id)) {

    return backing_.Enumerate(obj_id);

  }

  std::set<std::uint32_t> classes;

  for (auto class_id : writable_overlay_.Enumerate(obj_id)) {

    classes.insert(class_id);

  }

  for (auto class_id : backing_.Enumerate(obj_id)) {

    classes.insert(class_id);

  }

  return ae::ClassList{classes.begin(), classes.end()};

}



ae::DomainLoad OverlayDomainStorage::Load(ae::DomainQuery const& query) {

  if (IsSupplyConstant(query.id)) {

    return backing_.Load(query);

  }

  auto overlay_load = writable_overlay_.Load(query);

  if (overlay_load.result == ae::DomainLoadResult::kLoaded ||

      overlay_load.result == ae::DomainLoadResult::kRemoved) {

    return overlay_load;

  }

  return backing_.Load(query);

}



void OverlayDomainStorage::Remove(ae::ObjId const& obj_id) {

  if (IsSupplyConstant(obj_id)) {
    return;
  }

  writable_overlay_.Remove(obj_id);

}



void OverlayDomainStorage::CleanUp() { writable_overlay_.CleanUp(); }



}  // namespace apptraverse

