#include "apptraverse/publication_opener_label_trace.h"

#include <cstdio>

#include "aether-miscpp/serialization/serialization.h"
#include "aether-objects/obj/domain_buffer.h"
#include "aether-objects/obj/obj_ptr_base.h"

#if defined(__ANDROID__) && defined(APPTRAVERSE_A1R1_OBJPTR_WATCH)

#include <dlfcn.h>
#include <unwind.h>

namespace {

using ObjPtrArchive = ae::seri::BinaryArchive<ae::DomainBuffer>;

bool IsWatchedLabelObjPtr(ae::ObjectPtrBase* self) {
  apptraverse::PublicationOpenerLabelTrace& trace =
      apptraverse::PublicationOpenerLabelTrace::Instance();
  if (!trace.IsActive() || !trace.LabelIdWatchArmed()) {
    return false;
  }
  return static_cast<void*>(self) == trace.WatchedLabelObjPtrAddress();
}

void AppendWriterBacktrace(apptraverse::PublicationOpenerLabelTrace& trace) {
  void* frames[24];
  struct State {
    void** frames;
    int max;
    int count;
  } state{frames, 24, 0};
  _Unwind_Backtrace(
      +[](struct _Unwind_Context* ctx, void* arg) -> _Unwind_Reason_Code {
        auto* s = static_cast<State*>(arg);
        if (s->count >= s->max) {
          return _URC_END_OF_STACK;
        }
        s->frames[s->count++] = reinterpret_cast<void*>(_Unwind_GetIP(ctx));
        return _URC_NO_REASON;
      },
      &state);
  for (int i = 0; i < state.count && i < 16; ++i) {
    Dl_info info{};
    char line[256];
    if (dladdr(state.frames[i], &info) && info.dli_sname != nullptr) {
      std::snprintf(line, sizeof(line), "WRITER frame[%d]=%p %s+0x%lx", i,
                    state.frames[i], info.dli_sname,
                    static_cast<unsigned long>(
                        reinterpret_cast<char*>(state.frames[i]) -
                        reinterpret_cast<char*>(info.dli_saddr)));
    } else {
      std::snprintf(line, sizeof(line), "WRITER frame[%d]=%p", i, state.frames[i]);
    }
    trace.OnWatchedLabelObjPtrMutation(nullptr, line, 0, 0);
  }
}

void MaybeRecordMutation(ae::ObjectPtrBase* self, char const* op,
                         std::uint32_t before) {
  if (!IsWatchedLabelObjPtr(self)) {
    return;
  }
  std::uint32_t const after = self->id().id();
  if (before == after) {
    return;
  }
  apptraverse::PublicationOpenerLabelTrace& trace =
      apptraverse::PublicationOpenerLabelTrace::Instance();
  trace.OnWatchedLabelObjPtrMutation(self, op, before, after);
  AppendWriterBacktrace(trace);
}

}  // namespace

extern "C" {

ae::seri::SeriResult __real__ZN2ae4seri10SerializerINS0_13BinaryArchiveINS_12DomainBufferEEENS_13ObjectPtrBaseEvE6DeseriERS4_NS0_4MetaIS5_EE(
    ObjPtrArchive& archive, ae::seri::Meta<ae::ObjectPtrBase> meta);
ae::seri::SeriResult __wrap__ZN2ae4seri10SerializerINS0_13BinaryArchiveINS_12DomainBufferEEENS_13ObjectPtrBaseEvE6DeseriERS4_NS0_4MetaIS5_EE(
    ObjPtrArchive& archive, ae::seri::Meta<ae::ObjectPtrBase> meta) {
  ae::ObjectPtrBase* self = &meta.value;
  std::uint32_t const before = IsWatchedLabelObjPtr(self) ? self->id().id() : 0;
  ae::seri::SeriResult const result =
      __real__ZN2ae4seri10SerializerINS0_13BinaryArchiveINS_12DomainBufferEEENS_13ObjectPtrBaseEvE6DeseriERS4_NS0_4MetaIS5_EE(
          archive, meta);
  MaybeRecordMutation(self, "ObjectPtrBase::Deseri", before);
  return result;
}

}  // extern "C"

#endif
