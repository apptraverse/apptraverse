#include "apptraverse/publication_opener_lifetime_trace.h"

#include <cstdio>
#include <cstring>

#include "aether-objects/obj/obj.h"

#if defined(__ANDROID__)
#include <android/log.h>
#include <dlfcn.h>
#include <unwind.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

namespace apptraverse {
namespace {

void LogProbeLine(char const* line) {
#if defined(__ANDROID__)
  __android_log_print(ANDROID_LOG_INFO, "A1R1_PROBE", "%s", line);
#else
  (void)line;
#endif
}

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

bool MatchesOpener(PublicationOpenerLifetimeTrace const& trace,
                   std::uint32_t obj_id) {
  return trace.IsActive() && obj_id == trace.OpenerStorageId();
}

}  // namespace

PublicationOpenerLifetimeTrace& PublicationOpenerLifetimeTrace::Instance() {
  static PublicationOpenerLifetimeTrace instance;
  return instance;
}

void PublicationOpenerLifetimeTrace::Begin(std::uint32_t opener_storage_id) {
  active_ = true;
  opener_storage_id_ = opener_storage_id;
  construct_count_ = 0;
  remove_object_count_ = 0;
  dtor_count_ = 0;
  log_[0] = '\0';
}

void PublicationOpenerLifetimeTrace::End() { active_ = false; }

void PublicationOpenerLifetimeTrace::AppendLine(char* buffer,
                                                std::size_t buffer_size,
                                                char const* line) {
  if (line == nullptr || buffer_size == 0) {
    return;
  }
  std::size_t const used = std::strlen(buffer);
  if (used >= buffer_size - 1) {
    return;
  }
  std::snprintf(buffer + used, buffer_size - used, "%s\n", line);
  LogProbeLine(line);
}

void PublicationOpenerLifetimeTrace::AppendBacktrace(char* buffer,
                                                     std::size_t buffer_size) {
#if defined(__ANDROID__)
  void* frames[24];
  AndroidBacktraceState state{};
  state.frames = frames;
  state.max_frames = 24;
  _Unwind_Backtrace(AndroidBacktraceUnwindCallback, &state);
  for (int i = 0; i < state.count && i < 12; ++i) {
    Dl_info info{};
    if (dladdr(frames[i], &info) && info.dli_sname != nullptr) {
      char line[320];
      std::snprintf(line, sizeof(line), "  bt[%d]=%p %s+0x%lx", i, frames[i],
                    info.dli_sname,
                    static_cast<unsigned long>(
                        reinterpret_cast<char*>(frames[i]) -
                        reinterpret_cast<char*>(info.dli_saddr)));
      AppendLine(buffer, buffer_size, line);
    }
  }
#elif defined(_WIN32)
  void* frames[24];
  USHORT const n = CaptureStackBackTrace(0, 24, frames, nullptr);
  for (USHORT i = 0; i < n && i < 12; ++i) {
    char line[96];
    std::snprintf(line, sizeof(line), "  bt[%u]=%p", static_cast<unsigned>(i),
                  frames[i]);
    AppendLine(buffer, buffer_size, line);
  }
#endif
}

void PublicationOpenerLifetimeTrace::AppendReport(char* buffer,
                                                  std::size_t buffer_size) {
  if (buffer_size > 0) {
    buffer[0] = '\0';
  }
  AppendLine(buffer, buffer_size, "A1R1_OPENER_LIFETIME");
  char summary[256];
  std::snprintf(summary, sizeof(summary),
                "construct_count=%d remove_object_count=%d dtor_count=%d",
                construct_count_, remove_object_count_, dtor_count_);
  AppendLine(buffer, buffer_size, summary);
  if (log_[0] != '\0') {
    AppendLine(buffer, buffer_size, log_);
  }
}

void PublicationOpenerLifetimeTrace::OnConstructObj(void* domain, void* obj,
                                                      unsigned obj_id) {
  if (!MatchesOpener(*this, obj_id)) {
    return;
  }
  ++construct_count_;
  char line[256];
  std::snprintf(line, sizeof(line),
                "ConstructObj opener=%u this=%p domain=%p n=%d", obj_id, obj,
                domain, construct_count_);
  AppendLine(log_, sizeof(log_), line);
}

void PublicationOpenerLifetimeTrace::OnRemoveObject(void* domain, void* obj,
                                                    unsigned obj_id) {
  if (!MatchesOpener(*this, obj_id)) {
    return;
  }
  ++remove_object_count_;
  char line[256];
  std::snprintf(line, sizeof(line),
                "Domain::RemoveObject opener=%u this=%p domain=%p n=%d", obj_id,
                obj, domain, remove_object_count_);
  AppendLine(log_, sizeof(log_), line);
  if (remove_object_count_ == 1) {
    AppendBacktrace(log_, sizeof(log_));
  }
}

void PublicationOpenerLifetimeTrace::OnObjDtor(void* domain, void* obj,
                                               unsigned obj_id) {
  if (!MatchesOpener(*this, obj_id)) {
    return;
  }
  ++dtor_count_;
  char line[256];
  std::snprintf(line, sizeof(line), "Obj::~Obj opener=%u this=%p domain=%p n=%d",
                obj_id, obj, domain, dtor_count_);
  AppendLine(log_, sizeof(log_), line);
  if (dtor_count_ == 1 && remove_object_count_ == 0) {
    AppendBacktrace(log_, sizeof(log_));
  }
}

void PublicationOpenerLifetimeTrace::OnTryLoadStoredLayer(
    void* domain_graph, unsigned storage_id, unsigned class_id, int loaded,
    int cycle_deferred, int is_err, int err_code) {
  if (!MatchesOpener(*this, storage_id)) {
    return;
  }
  char line[320];
  std::snprintf(line, sizeof(line),
                "TryLoadStoredLayer storage=%u class=%u loaded=%d "
                "cycle_deferred=%d is_err=%d err_code=%d graph=%p",
                storage_id, class_id, loaded, cycle_deferred, is_err, err_code,
                domain_graph);
  AppendLine(log_, sizeof(log_), line);
  if (is_err) {
    AppendBacktrace(log_, sizeof(log_));
  }
}

}  // namespace apptraverse

extern "C" void apptraverse_a1r1_domain_construct_obj(void* domain, void* obj,
                                                      unsigned obj_id) {
  apptraverse::PublicationOpenerLifetimeTrace::Instance().OnConstructObj(
      domain, obj, obj_id);
}

extern "C" void apptraverse_a1r1_domain_remove_object(void* domain, void* obj,
                                                      unsigned obj_id) {
  apptraverse::PublicationOpenerLifetimeTrace::Instance().OnRemoveObject(
      domain, obj, obj_id);
}

extern "C" void apptraverse_a1r1_obj_dtor(void* domain, void* obj,
                                          unsigned obj_id) {
  apptraverse::PublicationOpenerLifetimeTrace::Instance().OnObjDtor(
      domain, obj, obj_id);
}

extern "C" void apptraverse_a1r1_try_load_stored_layer(
    void* domain_graph, unsigned storage_id, unsigned class_id, int loaded,
    int cycle_deferred, int is_err, int err_code) {
  apptraverse::PublicationOpenerLifetimeTrace::Instance().OnTryLoadStoredLayer(
      domain_graph, storage_id, class_id, loaded, cycle_deferred, is_err,
      err_code);
}
