#include "apptraverse/publication_opener_label_trace.h"

#include <cstddef>
#include <cstdio>
#include <cstring>

#include "aether-objects/obj/idomain_storage.h"
#include "aether-objects/obj/domain.h"
#include "aether-objects/obj/obj.h"
#include "aether-objects/obj/obj_ptr_base.h"

#include "apptraverse/object_serialization.h"
#include "apptraverse/ui/ui_push_button.h"

#if defined(__ANDROID__)
#include <dlfcn.h>
#include <unwind.h>
#elif defined(__linux__)
#include <execinfo.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

namespace apptraverse {
namespace detail {
struct ObjPtrLayoutProbe : ae::ObjectPtrBase {
  static void* IdStorageFromUiPushButton(void* ui_push_button_this) {
    if (ui_push_button_this == nullptr) {
      return nullptr;
    }
    auto& base = static_cast<ae::ObjectPtrBase&>(
        static_cast<ui::UiPushButton*>(ui_push_button_this)->label);
    return &static_cast<ObjPtrLayoutProbe&>(base).id_;
  }
  static std::size_t IdStorageSize() {
    ObjPtrLayoutProbe probe{};
    return sizeof(probe.id_);
  }
};
}  // namespace detail

namespace {

ObjPtrTraceSnapshot SnapshotObjPtr(ae::ObjectPtrBase const& ptr) {
  ObjPtrTraceSnapshot out{};
  out.field_addr = &ptr;
  out.id = ptr.id().id();
  out.flags = static_cast<std::uint32_t>(static_cast<ae::ObjFlags::Type>(ptr.flags()));
  out.valid = ptr.is_valid() ? 1 : 0;
  out.loaded = ptr.is_loaded() ? 1 : 0;
  out.cached_obj = ptr.cached().get();
  return out;
}

bool ReadU32(std::uint8_t const* data, std::size_t size, std::size_t& pos,
             std::uint32_t& out) {
  if (pos + 4 > size) {
    return false;
  }
  std::memcpy(&out, data + pos, 4);
  pos += 4;
  return true;
}

}  // namespace

PublicationOpenerLabelTrace& PublicationOpenerLabelTrace::Instance() {
  static PublicationOpenerLabelTrace instance;
  return instance;
}

void PublicationOpenerLabelTrace::BeginSession(ae::ObjId opener_id,
                                               ae::ObjId model_label_id) {
  active_ = true;
  opener_id_ = opener_id;
  model_label_id_ = model_label_id;
  wire_ = {};
  base_after_ = {};
  label_after_ = {};
  enabled_after_ = 0;
  load_enter_this_ = nullptr;
  load_enter_count_ = 0;
  layer_attempt_count_ = 0;
  first_cycle_deferred_class_ = 0;
  hydrate_passes_ = 0;
  hydrate_pass_index_ = 0;
  memory_snapshot_fn_ = nullptr;
  domain_storage_ = nullptr;
  anchor_opener_obj_ = nullptr;
  anchor_label_id_storage_ = nullptr;
  anchor_label_id_ = 0;
  last_observed_label_id_ = 0;
  last_label_id_valid_ = 0;
  label_id_watch_armed_ = 0;
  first_label_id_write_tag_[0] = '\0';
  first_label_id_write_before_ = 0;
  first_label_id_write_after_ = 0;
  first_label_id_write_pass_ = -1;
  first_label_id_write_hydrate_obj_ = 0;
  first_label_id_write_phase_[0] = '\0';
  first_domain_find_miss_tag_[0] = '\0';
  context_pass_ = -1;
  context_obj_id_ = 0;
  context_phase_[0] = '\0';
  first_obj_ptr_mutation_op_[0] = '\0';
  snapshots_[0] = '\0';
}

void PublicationOpenerLabelTrace::SetDomainStorage(ae::IDomainStorage* storage) {
  domain_storage_ = storage;
}

void PublicationOpenerLabelTrace::EndSession() { active_ = false; }

void PublicationOpenerLabelTrace::SetUiPushButtonLayerBytes(
    std::uint8_t const* data, std::size_t size) {
  wire_.layer_size = size;
  if (data == nullptr || size == 0) {
    return;
  }
  std::vector<std::uint8_t> copy(data, data + size);
  wire_layer_copy_ = std::move(copy);
}

void PublicationOpenerLabelTrace::DecodeWireFromLayerBytes() {
  wire_.decode_ok = 0;
  if (wire_layer_copy_.empty()) {
    return;
  }
  std::uint8_t const* data = wire_layer_copy_.data();
  std::size_t const size = wire_layer_copy_.size();
  std::size_t pos = 0;
  if (!ReadU32(data, size, pos, wire_.wire_base_id)) {
    return;
  }
  if (!ReadU32(data, size, pos, wire_.wire_base_flags)) {
    return;
  }
  wire_.pos_after_base = pos;
  if (!ReadU32(data, size, pos, wire_.wire_label_id)) {
    return;
  }
  if (!ReadU32(data, size, pos, wire_.wire_label_flags)) {
    return;
  }
  wire_.pos_after_label = pos;
  if (pos < size) {
    wire_.wire_enabled = data[pos] != 0 ? 1 : 0;
    ++pos;
  }
  wire_.pos_end = pos;
  wire_.decode_ok = 1;
}

void PublicationOpenerLabelTrace::OnUiPushButtonLoadEnter(
    void* ui_push_button_this, ae::ObjId storage_id) {
  if (!active_ || storage_id.id() != opener_id_.id()) {
    return;
  }
  SetHydrateContext(hydrate_pass_index_, storage_id.id(), "UiPushButton::Load");
  ++load_enter_count_;
  load_enter_this_ = ui_push_button_this;
  char line[256];
  std::snprintf(line, sizeof(line),
                "UiPushButton::Load enter storage_id=%u this=%p label_field=%p",
                storage_id.id(), ui_push_button_this,
                &static_cast<ui::UiPushButton*>(ui_push_button_this)->label);
  AppendLine(snapshots_, sizeof(snapshots_), line);
}

void PublicationOpenerLabelTrace::OnAfterBaseDeseri(void* ui_push_button_this) {
  if (!active_ || load_enter_this_ != ui_push_button_this) {
    return;
  }
  char line[160];
  std::snprintf(line, sizeof(line),
                "after base_ dnv: pos_wire_after_base=%zu wire_base_id=%u "
                "wire_base_flags=%u",
                wire_.pos_after_base, wire_.wire_base_id, wire_.wire_base_flags);
  AppendLine(snapshots_, sizeof(snapshots_), line);
}

void PublicationOpenerLabelTrace::OnAfterLabelDeseri(void* ui_push_button_this) {
  if (!active_ || load_enter_this_ != ui_push_button_this) {
    return;
  }
  auto* btn = static_cast<ui::UiPushButton*>(ui_push_button_this);
  label_after_ = SnapshotObjPtr(btn->label);
  char line[320];
  std::snprintf(
      line, sizeof(line),
      "after label dnv: label.id=%u flags=%u valid=%d loaded=%d cached=%p "
      "pos_wire=%zu wire_label_id=%u model_label_id=%u",
      label_after_.id, label_after_.flags, label_after_.valid, label_after_.loaded,
      label_after_.cached_obj, wire_.pos_after_label, wire_.wire_label_id,
      model_label_id_.id());
  AppendLine(snapshots_, sizeof(snapshots_), line);

  if (label_after_.valid && label_after_.id == model_label_id_.id()) {
    label_id_watch_armed_ = 1;
    anchor_opener_obj_ = ui_push_button_this;
    anchor_label_id_ = label_after_.id;
    last_observed_label_id_ = label_after_.id;
    last_label_id_valid_ = label_after_.valid;
    anchor_label_id_storage_ = label_after_.field_addr;
    char arm[192];
    std::snprintf(arm, sizeof(arm),
                  "label.id watch armed id=%u opener=%p label_field=%p id_storage=%p",
                  anchor_label_id_, anchor_opener_obj_, label_after_.field_addr,
                  anchor_label_id_storage_);
    AppendLine(snapshots_, sizeof(snapshots_), arm);
  }
}

void PublicationOpenerLabelTrace::OnAfterEnabledDeseri(void* ui_push_button_this) {
  if (!active_ || load_enter_this_ != ui_push_button_this) {
    return;
  }
  auto* btn = static_cast<ui::UiPushButton*>(ui_push_button_this);
  enabled_after_ = btn->enabled() ? 1 : 0;
  char line[128];
  std::snprintf(line, sizeof(line), "after enabled_ dnv: enabled=%d pos_wire=%zu",
                enabled_after_, wire_.pos_end);
  AppendLine(snapshots_, sizeof(snapshots_), line);
}

void PublicationOpenerLabelTrace::OnStoredLayerAttempt(
    ae::ObjId storage_id, std::uint32_t class_id, int loaded, int cycle_deferred) {
  if (!active_ || storage_id.id() != opener_id_.id()) {
    return;
  }
  ++layer_attempt_count_;
  if (cycle_deferred && first_cycle_deferred_class_ == 0) {
    first_cycle_deferred_class_ = static_cast<int>(class_id);
  }
  char line[160];
  std::snprintf(line, sizeof(line),
                "TryLoadStoredLayer storage=%u class=%u loaded=%d cycle_deferred=%d",
                storage_id.id(), class_id, loaded, cycle_deferred);
  AppendLine(snapshots_, sizeof(snapshots_), line);
}

void PublicationOpenerLabelTrace::SetOpenerMemorySnapshotFn(
    OpenerMemorySnapshotFn fn) {
  memory_snapshot_fn_ = fn;
}

namespace {

#if defined(__ANDROID__)
struct AndroidBacktraceState {
  void** frames{nullptr};
  int max_frames{0};
  int count{0};
};

_Unwind_Reason_Code AndroidBacktraceUnwindCallback(struct _Unwind_Context* context,
                                                   void* arg) {
  auto* state = static_cast<AndroidBacktraceState*>(arg);
  if (state->count >= state->max_frames) {
    return _URC_END_OF_STACK;
  }
  state->frames[state->count++] =
      reinterpret_cast<void*>(_Unwind_GetIP(context));
  return _URC_NO_REASON;
}
#endif

}  // namespace

void PublicationOpenerLabelTrace::AppendBacktrace(char* buffer,
                                                  std::size_t buffer_size) const {
#if defined(__ANDROID__)
  void* frames[24];
  AndroidBacktraceState state{};
  state.frames = frames;
  state.max_frames = 24;
  _Unwind_Backtrace(AndroidBacktraceUnwindCallback, &state);
  for (int i = 0; i < state.count && i < 16; ++i) {
    Dl_info info{};
    if (dladdr(frames[i], &info) && info.dli_sname != nullptr) {
      char line[256];
      std::snprintf(line, sizeof(line), "frame[%d]=%p %s+0x%lx", i, frames[i],
                    info.dli_sname,
                    static_cast<unsigned long>(
                        reinterpret_cast<char*>(frames[i]) -
                        reinterpret_cast<char*>(info.dli_saddr)));
      AppendLine(buffer, buffer_size, line);
    } else {
      char line[96];
      std::snprintf(line, sizeof(line), "frame[%d]=%p", i, frames[i]);
      AppendLine(buffer, buffer_size, line);
    }
  }
#elif defined(__linux__)
  void* frames[24];
  int const frame_count = backtrace(frames, 24);
  char** symbols = backtrace_symbols(frames, frame_count);
  if (symbols == nullptr) {
    return;
  }
  for (int i = 0; i < frame_count && i < 16; ++i) {
    AppendLine(buffer, buffer_size, symbols[i]);
  }
  std::free(symbols);
#elif defined(_WIN32)
  void* frames[24];
  USHORT const frame_count =
      CaptureStackBackTrace(0, 24, frames, nullptr);
  for (USHORT i = 0; i < frame_count && i < 16; ++i) {
    char line[96];
    std::snprintf(line, sizeof(line), "frame[%u]=%p", static_cast<unsigned>(i),
                  frames[i]);
    AppendLine(buffer, buffer_size, line);
  }
#endif
}

void PublicationOpenerLabelTrace::LogDomainFindDiagnostic(ae::Domain& domain,
                                                          char const* tag) {
  ae::Ptr<ae::Obj> const found = domain.Find(opener_id_);
  char storage_layers[128];
  storage_layers[0] = '\0';
  if (domain_storage_ != nullptr) {
    FormatDomainStorageClasses(*domain_storage_, opener_id_, storage_layers,
                               sizeof(storage_layers));
  }
  char raw_line[256];
  raw_line[0] = '\0';
  if (anchor_opener_obj_ != nullptr) {
    auto* raw = static_cast<ae::Obj*>(anchor_opener_obj_);
    std::snprintf(raw_line, sizeof(raw_line),
                  " anchor_raw_obj=%p anchor_raw_obj_id=%u anchor_matches=%d",
                  static_cast<void*>(raw), raw->obj_id.id(),
                  raw->obj_id.id() == opener_id_.id() ? 1 : 0);
  }
  char line[512];
  std::snprintf(line, sizeof(line),
                "DOMAIN_FIND %s: Find(opener)=%p storage=[%s]%s", tag,
                found.get(), storage_layers, raw_line);
  AppendLine(snapshots_, sizeof(snapshots_), line);
  if (!found && first_domain_find_miss_tag_[0] == '\0') {
    std::snprintf(first_domain_find_miss_tag_, sizeof(first_domain_find_miss_tag_),
                  "%s", tag);
  }
}

void PublicationOpenerLabelTrace::NoteLabelIdObservation(
    ae::Domain& domain, char const* tag, int pass, std::uint32_t hydrate_obj_id,
    char const* phase) {
  if (!active_ || !label_id_watch_armed_ || memory_snapshot_fn_ == nullptr) {
    return;
  }
  OpenerMemorySnapshot const snap = memory_snapshot_fn_(domain, opener_id_);
  std::uint32_t const observed_id = snap.label.id;
  int const observed_valid = snap.label.valid;

  if (snap.opener_obj == nullptr) {
    LogDomainFindDiagnostic(domain, tag);
  } else if (anchor_opener_obj_ != nullptr && snap.opener_obj != anchor_opener_obj_) {
    char line[256];
    std::snprintf(line, sizeof(line),
                  "OPENER_LIFETIME %s: Find obj=%p != anchor=%p (reallocated?)",
                  tag, snap.opener_obj, anchor_opener_obj_);
    AppendLine(snapshots_, sizeof(snapshots_), line);
  }

  if (anchor_label_id_ != 0 && last_observed_label_id_ == anchor_label_id_ &&
      observed_id != anchor_label_id_ && first_label_id_write_tag_[0] == '\0' &&
      snap.opener_obj != nullptr) {
    std::snprintf(first_label_id_write_tag_, sizeof(first_label_id_write_tag_), "%s",
                  tag);
    first_label_id_write_before_ = last_observed_label_id_;
    first_label_id_write_after_ = observed_id;
    first_label_id_write_pass_ = pass;
    first_label_id_write_hydrate_obj_ = hydrate_obj_id;
    std::snprintf(first_label_id_write_phase_, sizeof(first_label_id_write_phase_),
                  "%s", phase != nullptr ? phase : "");
    char hit[320];
    std::snprintf(
        hit, sizeof(hit),
        "FIRST_LABEL_ID_WRITE at %s pass=%d hydrate_obj=%u phase=%s: id %u->%u "
        "valid %d->%d opener=%p",
        tag, pass, hydrate_obj_id, phase != nullptr ? phase : "", first_label_id_write_before_,
        first_label_id_write_after_, last_label_id_valid_, observed_valid,
        snap.opener_obj);
    AppendLine(snapshots_, sizeof(snapshots_), hit);
    AppendBacktrace(snapshots_, sizeof(snapshots_));
  }

  last_observed_label_id_ = observed_id;
  last_label_id_valid_ = observed_valid;
}

void PublicationOpenerLabelTrace::SetHydrateContext(int pass, std::uint32_t obj_id,
                                                    char const* phase) {
  context_pass_ = pass;
  context_obj_id_ = obj_id;
  if (phase != nullptr) {
    std::snprintf(context_phase_, sizeof(context_phase_), "%s", phase);
  } else {
    context_phase_[0] = '\0';
  }
}

void PublicationOpenerLabelTrace::GetHydrateContext(int& pass, std::uint32_t& obj_id,
                                                    char* phase,
                                                    std::size_t phase_size) const {
  pass = context_pass_;
  obj_id = context_obj_id_;
  if (phase_size > 0) {
    std::snprintf(phase, phase_size, "%s", context_phase_);
  }
}

void PublicationOpenerLabelTrace::OnWatchedLabelObjPtrMutation(
    void* obj_ptr_this, char const* op, std::uint32_t id_before,
    std::uint32_t id_after) {
  if (!active_) {
    return;
  }
  if (obj_ptr_this == nullptr) {
    AppendLine(snapshots_, sizeof(snapshots_), op);
    return;
  }
  char line[384];
  std::snprintf(line, sizeof(line),
                "OBJPTR_MUTATION %s this=%p id %u->%u ctx_pass=%d ctx_obj=%u "
                "ctx_phase=%s opener=%p",
                op, obj_ptr_this, id_before, id_after, context_pass_, context_obj_id_,
                context_phase_, anchor_opener_obj_);
  AppendLine(snapshots_, sizeof(snapshots_), line);
  if (first_obj_ptr_mutation_op_[0] == '\0' && anchor_label_id_ != 0 &&
      id_before == anchor_label_id_ && id_after != anchor_label_id_) {
    std::snprintf(first_obj_ptr_mutation_op_, sizeof(first_obj_ptr_mutation_op_),
                  "%s", op);
  }
}

void PublicationOpenerLabelTrace::ObserveHydrateOperation(ae::Domain& domain,
                                                          int pass,
                                                          std::uint32_t obj_id,
                                                          char const* phase) {
  if (!active_) {
    return;
  }
  SetHydrateContext(pass, obj_id, phase);
  hydrate_pass_index_ = pass;
  char tag[128];
  std::snprintf(tag, sizeof(tag), "pass=%d obj=%u %s", pass, obj_id, phase);
  NoteLabelIdObservation(domain, tag, pass, obj_id, phase);
  RecordOpenerMemorySnapshot(domain, tag);
}

void PublicationOpenerLabelTrace::OnOpenerNodeLoadPhase(void* node_this,
                                                        char const* phase) {
  if (!active_ || node_this == nullptr) {
    return;
  }
  auto* obj = static_cast<ae::Obj*>(node_this);
  if (obj->obj_id.id() != opener_id_.id()) {
    return;
  }
  auto* btn = static_cast<ui::UiPushButton*>(node_this);
  ObjPtrTraceSnapshot const label = SnapshotObjPtr(btn->label);
  char line[256];
  std::snprintf(line, sizeof(line),
                "Node::Load opener %s this=%p label.id=%u valid=%d flags=%u",
                phase, node_this, label.id, label.valid, label.flags);
  AppendLine(snapshots_, sizeof(snapshots_), line);
  if (label_id_watch_armed_ && label.id != last_observed_label_id_ &&
      last_observed_label_id_ == anchor_label_id_ && label.id == 0 &&
      first_label_id_write_tag_[0] == '\0') {
    std::snprintf(first_label_id_write_tag_, sizeof(first_label_id_write_tag_),
                  "Node::Load:%s", phase);
    first_label_id_write_before_ = last_observed_label_id_;
    first_label_id_write_after_ = label.id;
    first_label_id_write_pass_ = hydrate_pass_index_;
    first_label_id_write_hydrate_obj_ = opener_id_.id();
    std::snprintf(first_label_id_write_phase_, sizeof(first_label_id_write_phase_),
                  "Node::Load:%s", phase);
    AppendBacktrace(snapshots_, sizeof(snapshots_));
  }
  last_observed_label_id_ = label.id;
  last_label_id_valid_ = label.valid;
}

void PublicationOpenerLabelTrace::OnMinimalAuxiliaryOpenerLoadPhase(
    void* opener_this, char const* phase) {
  if (!active_ || opener_this == nullptr) {
    return;
  }
  auto* obj = static_cast<ae::Obj*>(opener_this);
  if (obj->obj_id.id() != opener_id_.id()) {
    return;
  }
  auto* btn = static_cast<ui::UiPushButton*>(opener_this);
  ObjPtrTraceSnapshot const label = SnapshotObjPtr(btn->label);
  char line[288];
  std::snprintf(
      line, sizeof(line),
      "MinimalAuxiliaryOpenerButton::Load opener %s this=%p label.id=%u valid=%d "
      "flags=%u",
      phase, opener_this, label.id, label.valid, label.flags);
  AppendLine(snapshots_, sizeof(snapshots_), line);
  if (label_id_watch_armed_ && label.id != last_observed_label_id_ &&
      last_observed_label_id_ == anchor_label_id_ && label.id == 0 &&
      first_label_id_write_tag_[0] == '\0') {
    std::snprintf(first_label_id_write_tag_, sizeof(first_label_id_write_tag_),
                  "MinimalAuxiliaryOpenerButton::Load:%s", phase);
    first_label_id_write_before_ = last_observed_label_id_;
    first_label_id_write_after_ = label.id;
    first_label_id_write_pass_ = hydrate_pass_index_;
    first_label_id_write_hydrate_obj_ = opener_id_.id();
    std::snprintf(first_label_id_write_phase_, sizeof(first_label_id_write_phase_),
                  "MinimalAuxiliaryOpenerButton::Load:%s", phase);
    AppendBacktrace(snapshots_, sizeof(snapshots_));
  }
  last_observed_label_id_ = label.id;
  last_label_id_valid_ = label.valid;
}

void PublicationOpenerLabelTrace::RecordOpenerMemorySnapshot(ae::Domain& domain,
                                                             char const* tag) {
  if (!active_ || memory_snapshot_fn_ == nullptr) {
    return;
  }
  OpenerMemorySnapshot const snap = memory_snapshot_fn_(domain, opener_id_);
  if (snap.opener_obj == nullptr) {
    LogDomainFindDiagnostic(domain, tag);
  }
  char line[384];
  std::snprintf(line, sizeof(line),
                "%s: opener_obj=%p ui_push_button=%p label_field=%p id=%u "
                "valid=%d loaded=%d flags=%u cached=%p",
                tag, snap.opener_obj, snap.ui_push_button_this, snap.label_field_addr,
                snap.label.id, snap.label.valid, snap.label.loaded, snap.label.flags,
                snap.label.cached_obj);
  AppendLine(snapshots_, sizeof(snapshots_), line);
}

void PublicationOpenerLabelTrace::OnHydratePassEnd(int pass,
                                                   int pending_incomplete) {
  if (!active_) {
    return;
  }
  hydrate_passes_ = pass + 1;
  char line[96];
  std::snprintf(line, sizeof(line), "hydrate pass=%d pending_incomplete=%d", pass,
                pending_incomplete);
  AppendLine(snapshots_, sizeof(snapshots_), line);
}

void PublicationOpenerLabelTrace::AppendLine(char* buffer, std::size_t buffer_size,
                                             char const* line) const {
  if (buffer_size == 0) {
    return;
  }
  std::size_t const used = std::strlen(buffer);
  if (used >= buffer_size - 1) {
    return;
  }
  if (used > 0) {
    std::snprintf(buffer + used, buffer_size - used, "\n%s", line);
  } else {
    std::snprintf(buffer, buffer_size, "%s", line);
  }
}

void PublicationOpenerLabelTrace::FormatReport(char* buffer,
                                               std::size_t buffer_size) const {
  if (buffer_size == 0) {
    return;
  }
  buffer[0] = '\0';
  char header[512];
  std::snprintf(
      header, sizeof(header),
      "A1R1 opener trace opener_id=%u model_label_id=%u wire_size=%zu "
      "decode_ok=%d wire_label_id=%u wire_label_flags=%u wire_base_id=%u "
      "load_enter_count=%d first_cycle_deferred_class=%d hydrate_passes=%d",
      opener_id_.id(), model_label_id_.id(), wire_.layer_size, wire_.decode_ok,
      wire_.wire_label_id, wire_.wire_label_flags, wire_.wire_base_id,
      load_enter_count_, first_cycle_deferred_class_, hydrate_passes_);
  AppendLine(buffer, buffer_size, header);
  if (wire_.decode_ok && wire_.wire_label_id != model_label_id_.id()) {
    AppendLine(buffer, buffer_size,
               "OBS: wire label ObjId != model label ObjId before UI hydrate");
  }
  if (load_enter_count_ == 0) {
    AppendLine(buffer, buffer_size,
               "OBS: UiPushButton::Load never entered for opener (stored layer not "
               "applied via factory load)");
  } else if (label_after_.valid == 0) {
    AppendLine(buffer, buffer_size,
               "OBS: after label dnv label ref invalid (deseri or flags)");
  }
  if (first_label_id_write_tag_[0] != '\0') {
    char hit[256];
    std::snprintf(
        hit, sizeof(hit),
        "OBS: first_label_id_write tag=%s pass=%d hydrate_obj=%u phase=%s id %u->%u",
        first_label_id_write_tag_, first_label_id_write_pass_,
        first_label_id_write_hydrate_obj_, first_label_id_write_phase_,
        first_label_id_write_before_, first_label_id_write_after_);
    AppendLine(buffer, buffer_size, hit);
  }
  if (first_domain_find_miss_tag_[0] != '\0') {
    char miss[128];
    std::snprintf(miss, sizeof(miss), "OBS: first_domain_find_miss_at=%s",
                  first_domain_find_miss_tag_);
    AppendLine(buffer, buffer_size, miss);
  }
  if (first_obj_ptr_mutation_op_[0] != '\0') {
    char mut[128];
    std::snprintf(mut, sizeof(mut), "OBS: first_obj_ptr_mutation=%s",
                  first_obj_ptr_mutation_op_);
    AppendLine(buffer, buffer_size, mut);
  }
  AppendLine(buffer, buffer_size, snapshots_);
}

}  // namespace apptraverse

extern "C" void* a1r1_debug_label_id_storage(void* ui_push_button_this) {
  return apptraverse::detail::ObjPtrLayoutProbe::IdStorageFromUiPushButton(
      ui_push_button_this);
}

extern "C" std::size_t a1r1_debug_label_id_storage_size() {
  return apptraverse::detail::ObjPtrLayoutProbe::IdStorageSize();
}
