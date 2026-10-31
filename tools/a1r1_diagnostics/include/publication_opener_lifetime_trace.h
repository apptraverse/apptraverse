#ifndef APPTRAVERSE_PUBLICATION_OPENER_LIFETIME_TRACE_H_
#define APPTRAVERSE_PUBLICATION_OPENER_LIFETIME_TRACE_H_

#include <cstddef>
#include <cstdint>

namespace apptraverse {

class PublicationOpenerLifetimeTrace {
 public:
  static PublicationOpenerLifetimeTrace& Instance();

  void Begin(std::uint32_t opener_storage_id);
  void End();

  bool IsActive() const { return active_; }
  std::uint32_t OpenerStorageId() const { return opener_storage_id_; }

  void AppendReport(char* buffer, std::size_t buffer_size);

  void OnConstructObj(void* domain, void* obj, unsigned obj_id);
  void OnRemoveObject(void* domain, void* obj, unsigned obj_id);
  void OnObjDtor(void* domain, void* obj, unsigned obj_id);
  void OnTryLoadStoredLayer(void* domain_graph, unsigned storage_id,
                            unsigned class_id, int loaded, int cycle_deferred,
                            int is_err, int err_code);

 private:
  void AppendLine(char* buffer, std::size_t buffer_size, char const* line);
  void AppendBacktrace(char* buffer, std::size_t buffer_size);

  bool active_{false};
  std::uint32_t opener_storage_id_{0};
  int construct_count_{0};
  int remove_object_count_{0};
  int dtor_count_{0};
  char log_[12288]{};
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_PUBLICATION_OPENER_LIFETIME_TRACE_H_
