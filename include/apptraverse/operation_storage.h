#ifndef APPTRAVERSE_OPERATION_STORAGE_H_
#define APPTRAVERSE_OPERATION_STORAGE_H_

#include <cassert>
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <utility>
#include <vector>

#include "aether-miscpp/serialization/binary_archive.h"
#include "aether-miscpp/serialization/serialization.h"
#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"
#include "aether-objects/obj/idomain_storage.h"
#include "aether-objects/obj/obj.h"
#include "aether-objects/obj/obj_id.h"
#include "aether-objects/obj/registry.h"

namespace apptraverse::detail {

enum class OperationMode : std::uint8_t {
  kValidate,
  kRemap,
};

class OperationStorageWriter;

inline OperationStorageWriter* GetOperationStorageWriter(
    ae::IDomainStorageWriter* writer);

class OperationStorageWriter final : public ae::IDomainStorageWriter {
 public:
  OperationStorageWriter(
      OperationMode mode,
      ae::RamDomainStorage const* validation_storage,
      std::map<ae::ObjId, std::uint32_t> const* most_derived_classes,
      ae::Domain* target_domain,
      std::map<ae::ObjId, ae::ObjId> const* old_to_new,
      std::map<ae::ObjId, ae::Ptr<ae::Obj>> const* new_id_to_obj,
      std::unique_ptr<ae::IDomainStorageWriter> real_writer,
      bool* ok)
      : mode{mode},
        validation_storage{validation_storage},
        most_derived_classes{most_derived_classes},
        target_domain{target_domain},
        old_to_new{old_to_new},
        new_id_to_obj{new_id_to_obj},
        real_writer{std::move(real_writer)},
        ok{ok} {}

  ae::seri::SeriResult Write(ae::seri::SizeWriteTag data) override {
    if (real_writer != nullptr) {
      return real_writer->Write(data);
    }
    return ae::Ok{ae::seri::good};
  }

  ae::seri::SeriResult Write(ae::seri::DataWriteTag data) override {
    // Magic check to identify OperationStorageWriter without C++ RTTI
    if (data.size == 0 && data.data != nullptr) {
      *reinterpret_cast<OperationStorageWriter**>(
          const_cast<void*>(data.data)) = this;
      return ae::Ok{ae::seri::good};
    }
    if (real_writer != nullptr) {
      return real_writer->Write(data);
    }
    return ae::Ok{ae::seri::good};
  }

  OperationMode mode;
  ae::RamDomainStorage const* validation_storage{nullptr};
  std::map<ae::ObjId, std::uint32_t> const* most_derived_classes{nullptr};
  ae::Domain* target_domain{nullptr};
  std::map<ae::ObjId, ae::ObjId> const* old_to_new{nullptr};
  std::map<ae::ObjId, ae::Ptr<ae::Obj>> const* new_id_to_obj{nullptr};
  std::unique_ptr<ae::IDomainStorageWriter> real_writer;
  bool* ok{nullptr};
};

inline OperationStorageWriter* GetOperationStorageWriter(
    ae::IDomainStorageWriter* writer) {
  if (writer == nullptr) {
    return nullptr;
  }
  OperationStorageWriter* op = nullptr;
  writer->Write(ae::seri::DataWriteTag{&op, 0});
  return op;
}

class OperationStorage final : public ae::IDomainStorage {
 public:
  OperationStorage(
      OperationMode mode,
      ae::RamDomainStorage const* validation_storage,
      std::map<ae::ObjId, std::uint32_t> const* most_derived_classes,
      ae::Domain* target_domain,
      ae::IDomainStorage* real_storage,
      std::map<ae::ObjId, ae::ObjId> const* old_to_new,
      std::map<ae::ObjId, ae::Ptr<ae::Obj>> const* new_id_to_obj,
      bool* ok)
      : mode_{mode},
        validation_storage_{validation_storage},
        most_derived_classes_{most_derived_classes},
        target_domain_{target_domain},
        real_storage_{real_storage},
        old_to_new_{old_to_new},
        new_id_to_obj_{new_id_to_obj},
        ok_{ok} {}

  std::unique_ptr<ae::IDomainStorageWriter> Store(
      ae::DomainQuery const& query) override {
    std::unique_ptr<ae::IDomainStorageWriter> real_writer;
    if (real_storage_ != nullptr) {
      real_writer = real_storage_->Store(query);
    }
    return std::make_unique<OperationStorageWriter>(
        mode_, validation_storage_, most_derived_classes_, target_domain_,
        old_to_new_, new_id_to_obj_, std::move(real_writer), ok_);
  }

  ae::ClassList Enumerate(ae::ObjId const&) override { return {}; }
  ae::DomainLoad Load(ae::DomainQuery const&) override {
    return ae::DomainLoad{ae::DomainLoadResult::kEmpty, nullptr};
  }
  void Remove(ae::ObjId const&) override {}
  void CleanUp() override {}

 private:
  OperationMode mode_;
  ae::RamDomainStorage const* validation_storage_{nullptr};
  std::map<ae::ObjId, std::uint32_t> const* most_derived_classes_{nullptr};
  ae::Domain* target_domain_{nullptr};
  ae::IDomainStorage* real_storage_{nullptr};
  std::map<ae::ObjId, ae::ObjId> const* old_to_new_{nullptr};
  std::map<ae::ObjId, ae::Ptr<ae::Obj>> const* new_id_to_obj_{nullptr};
  bool* ok_{nullptr};
};

template <typename TargetType, bool IsLocal, typename PtrType>
ae::seri::SeriResult HandlePointerSeri(
    ae::seri::BinaryArchive<ae::DomainBuffer>& archive,
    PtrType const& ptr_ref,
    OperationStorageWriter* op) {
  (void)archive;
  if (op->mode == OperationMode::kValidate) {
    if constexpr (IsLocal) {
      if (ptr_ref.is_valid()) {
        if (op->ok != nullptr) {
          *op->ok = false;
        }
        return ae::Error{ae::seri::write_error};
      }
      return ae::Ok{ae::seri::good};
    } else {
      if (!ptr_ref.is_valid()) {
        return ae::Ok{ae::seri::good};
      }
      auto const id = ptr_ref.id();
      if (op->validation_storage == nullptr) {
        if (op->ok != nullptr) {
          *op->ok = false;
        }
        return ae::Error{ae::seri::write_error};
      }
      auto it = op->validation_storage->state.find(id);
      if (it == op->validation_storage->state.end() || !it->second.has_value()) {
        if (op->ok != nullptr) {
          *op->ok = false;
        }
        return ae::Error{ae::seri::write_error};
      }
      if (op->most_derived_classes == nullptr) {
        if (op->ok != nullptr) {
          *op->ok = false;
        }
        return ae::Error{ae::seri::write_error};
      }
      auto class_it = op->most_derived_classes->find(id);
      if (class_it == op->most_derived_classes->end()) {
        if (op->ok != nullptr) {
          *op->ok = false;
        }
        return ae::Error{ae::seri::write_error};
      }
      auto const derived_class_id = class_it->second;
      auto& registry = ae::Registry::GetRegistry();
      if (registry.GenerationDistance(TargetType::kClassId, derived_class_id) < 0) {
        if (op->ok != nullptr) {
          *op->ok = false;
        }
        return ae::Error{ae::seri::write_error};
      }
      if (!ptr_ref.is_loaded()) {
        if (op->ok != nullptr) {
          *op->ok = false;
        }
        return ae::Error{ae::seri::write_error};
      }
      return ae::Ok{ae::seri::good};
    }
  } else if (op->mode == OperationMode::kRemap) {
    auto& mut_ptr = const_cast<PtrType&>(ptr_ref);
    if constexpr (IsLocal) {
      mut_ptr.Reset();
      ae::ObjId const empty_id{};
      ae::ObjFlags const empty_flags{};
      if (op->real_writer != nullptr) {
        if (auto res = op->real_writer->Write(
                ae::seri::DataWriteTag{&empty_id, sizeof(empty_id)});
            res.IsErr()) {
          return res;
        }
        if (auto res = op->real_writer->Write(
                ae::seri::DataWriteTag{&empty_flags, sizeof(empty_flags)});
            res.IsErr()) {
          return res;
        }
      }
      return ae::Ok{ae::seri::good};
    } else {
      if (!mut_ptr.is_valid()) {
        ae::ObjId const empty_id{};
        ae::ObjFlags const flags = mut_ptr.flags();
        if (op->real_writer != nullptr) {
          if (auto res = op->real_writer->Write(
                  ae::seri::DataWriteTag{&empty_id, sizeof(empty_id)});
              res.IsErr()) {
            return res;
          }
          if (auto res = op->real_writer->Write(
                  ae::seri::DataWriteTag{&flags, sizeof(flags)});
              res.IsErr()) {
            return res;
          }
        }
        return ae::Ok{ae::seri::good};
      }
      auto const old_id = mut_ptr.id();
      if (op->old_to_new == nullptr) {
        if (op->ok != nullptr) {
          *op->ok = false;
        }
        return ae::Error{ae::seri::write_error};
      }
      auto it = op->old_to_new->find(old_id);
      if (it == op->old_to_new->end()) {
        // If not in old_to_new, check if it was already remapped to one of new IDs
        if (op->new_id_to_obj != nullptr &&
            op->new_id_to_obj->find(old_id) != op->new_id_to_obj->end()) {
          ae::ObjFlags const flags = mut_ptr.flags();
          if (op->real_writer != nullptr) {
            if (auto res = op->real_writer->Write(
                    ae::seri::DataWriteTag{&old_id, sizeof(old_id)});
                res.IsErr()) {
              return res;
            }
            if (auto res = op->real_writer->Write(
                    ae::seri::DataWriteTag{&flags, sizeof(flags)});
                res.IsErr()) {
              return res;
            }
          }
          return ae::Ok{ae::seri::good};
        }
        if (op->ok != nullptr) {
          *op->ok = false;
        }
        return ae::Error{ae::seri::write_error};
      }
      auto const new_target_id = it->second;
      ae::Ptr<ae::Obj> target_obj;
      if (op->new_id_to_obj != nullptr) {
        auto obj_it = op->new_id_to_obj->find(new_target_id);
        if (obj_it != op->new_id_to_obj->end()) {
          target_obj = obj_it->second;
        }
      }
      if (!target_obj && op->target_domain != nullptr) {
        target_obj = op->target_domain->Find(new_target_id);
      }
      mut_ptr = PtrType{op->target_domain, new_target_id, mut_ptr.flags(),
                        target_obj};
      ae::ObjFlags const flags = mut_ptr.flags();
      if (op->real_writer != nullptr) {
        if (auto res = op->real_writer->Write(
                ae::seri::DataWriteTag{&new_target_id, sizeof(new_target_id)});
            res.IsErr()) {
          return res;
        }
        if (auto res = op->real_writer->Write(
                ae::seri::DataWriteTag{&flags, sizeof(flags)});
            res.IsErr()) {
          return res;
        }
      }
      return ae::Ok{ae::seri::good};
    }
  }
  return ae::Ok{ae::seri::good};
}

}  // namespace apptraverse::detail

#endif  // APPTRAVERSE_OPERATION_STORAGE_H_
