#ifndef APPTRAVERSE_PUBLICATION_OPENER_LABEL_TRACE_H_
#define APPTRAVERSE_PUBLICATION_OPENER_LABEL_TRACE_H_

#include <cstddef>
#include <cstdint>
#include <vector>

#include "aether-objects/obj/obj_id.h"

namespace ae {
class Domain;
class IDomainStorage;
}

namespace apptraverse {

constexpr std::uint32_t kPublicationTraceUiPushButtonClassId = 1491514641u;
constexpr std::uint32_t kPublicationTraceOpenerObjId = 0x07A10011u;

struct ObjPtrTraceSnapshot {
  void const* field_addr{nullptr};
  std::uint32_t id{0};
  std::uint32_t flags{0};
  int valid{0};
  int loaded{0};
  void const* cached_obj{nullptr};
};

struct OpenerMemorySnapshot {
  void* opener_obj{nullptr};
  void* ui_push_button_this{nullptr};
  void const* label_field_addr{nullptr};
  ObjPtrTraceSnapshot label{};
};

using OpenerMemorySnapshotFn = OpenerMemorySnapshot (*)(ae::Domain& domain,
                                                          ae::ObjId opener_id);

struct UiPushButtonWireSnapshot {
  std::size_t layer_size{0};
  std::size_t pos_after_base{0};
  std::size_t pos_after_label{0};
  std::size_t pos_end{0};
  std::uint32_t wire_base_id{0};
  std::uint32_t wire_base_flags{0};
  std::uint32_t wire_label_id{0};
  std::uint32_t wire_label_flags{0};
  int wire_enabled{0};
  int decode_ok{0};
};

class PublicationOpenerLabelTrace {
 public:
  static PublicationOpenerLabelTrace& Instance();

  void BeginSession(ae::ObjId opener_id, ae::ObjId model_label_id);
  void EndSession();

  bool IsActive() const { return active_; }

  void SetUiPushButtonLayerBytes(std::uint8_t const* data, std::size_t size);
  void DecodeWireFromLayerBytes();

  void OnUiPushButtonLoadEnter(void* ui_push_button_this, ae::ObjId storage_id);
  void OnAfterBaseDeseri(void* ui_push_button_this);
  void OnAfterLabelDeseri(void* ui_push_button_this);
  void OnAfterEnabledDeseri(void* ui_push_button_this);

  void OnStoredLayerAttempt(ae::ObjId storage_id, std::uint32_t class_id,
                            int loaded, int cycle_deferred);
  void OnHydratePassEnd(int pass, int pending_incomplete);

  void SetOpenerMemorySnapshotFn(OpenerMemorySnapshotFn fn);
  void SetDomainStorage(ae::IDomainStorage* storage);

  ae::ObjId OpenerStorageId() const { return opener_id_; }

  void RecordOpenerMemorySnapshot(ae::Domain& domain, char const* tag);
  void ObserveHydrateOperation(ae::Domain& domain, int pass, std::uint32_t obj_id,
                               char const* phase);
  void OnOpenerNodeLoadPhase(void* node_this, char const* phase);
  void OnMinimalAuxiliaryOpenerLoadPhase(void* opener_this, char const* phase);

  bool LabelIdWatchArmed() const { return label_id_watch_armed_ != 0; }
  void const* WatchedLabelObjPtrAddress() const {
    return anchor_label_id_storage_;
  }

  void SetHydrateContext(int pass, std::uint32_t obj_id, char const* phase);
  void GetHydrateContext(int& pass, std::uint32_t& obj_id, char* phase,
                         std::size_t phase_size) const;

  void OnWatchedLabelObjPtrMutation(void* obj_ptr_this, char const* op,
                                    std::uint32_t id_before,
                                    std::uint32_t id_after);

  void FormatReport(char* buffer, std::size_t buffer_size) const;

  UiPushButtonWireSnapshot const& wire() const { return wire_; }

 private:
  void AppendLine(char* buffer, std::size_t buffer_size, char const* line) const;
  void AppendBacktrace(char* buffer, std::size_t buffer_size) const;
  void NoteLabelIdObservation(ae::Domain& domain, char const* tag, int pass,
                              std::uint32_t hydrate_obj_id, char const* phase);
  void LogDomainFindDiagnostic(ae::Domain& domain, char const* tag);

  bool active_{false};
  ae::ObjId opener_id_{};
  ae::ObjId model_label_id_{};
  UiPushButtonWireSnapshot wire_{};
  ObjPtrTraceSnapshot base_after_{};
  ObjPtrTraceSnapshot label_after_{};
  int enabled_after_{0};
  void* load_enter_this_{nullptr};
  int load_enter_count_{0};
  int layer_attempt_count_{0};
  int first_cycle_deferred_class_{0};
  int hydrate_passes_{0};
  int hydrate_pass_index_{0};
  char snapshots_[16384]{};
  std::vector<std::uint8_t> wire_layer_copy_{};
  OpenerMemorySnapshotFn memory_snapshot_fn_{nullptr};
  ae::IDomainStorage* domain_storage_{nullptr};
  void* anchor_opener_obj_{nullptr};
  void const* anchor_label_id_storage_{nullptr};
  std::uint32_t anchor_label_id_{0};
  std::uint32_t last_observed_label_id_{0};
  int last_label_id_valid_{0};
  int label_id_watch_armed_{0};
  char first_label_id_write_tag_[128]{};
  std::uint32_t first_label_id_write_before_{0};
  std::uint32_t first_label_id_write_after_{0};
  int first_label_id_write_pass_{-1};
  std::uint32_t first_label_id_write_hydrate_obj_{0};
  char first_label_id_write_phase_[64]{};
  char first_domain_find_miss_tag_[64]{};
  int context_pass_{-1};
  std::uint32_t context_obj_id_{0};
  char context_phase_[64]{};
  char first_obj_ptr_mutation_op_[64]{};
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_PUBLICATION_OPENER_LABEL_TRACE_H_
