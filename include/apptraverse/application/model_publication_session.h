#ifndef APPTRAVERSE_APPLICATION_MODEL_PUBLICATION_SESSION_H_
#define APPTRAVERSE_APPLICATION_MODEL_PUBLICATION_SESSION_H_

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <mutex>
#include <vector>

#include "aether-objects/obj/domain.h"
#include "aether-objects/obj/idomain_storage.h"

#include "apptraverse/application/model_checkpoint_persistence.h"
#include "apptraverse/model_object_proxy.h"
#include "apptraverse/node.h"
#include "apptraverse/presentation_size_queue.h"
#include "apptraverse/publication_channel.h"

namespace apptraverse {

class DirectoryDomainStorage;
enum class ModelPublicationKind {
  Initial,
  Incremental,
};

struct ModelPublicationSessionConfig {
  ae::ObjId root_object_id;
  std::function<void()> ensure_type_registration;
  std::function<ae::Obj*(ae::Domain& domain)> load_mutable_root;
  // Empty typed root shell in domain before packed checkpoint hydration.
  std::function<ae::Obj*(ae::Domain& domain)> create_checkpoint_root_shell;
  // Optional: create distilled state when state_dir has no root layer.
  std::function<void(ae::Domain& domain, ae::IDomainStorage& storage)>
      bootstrap_if_missing;
  // Called on model thread after root is loaded (runtime-only hooks).
  std::function<void(ae::Obj& root)> on_model_root_loaded;
  std::function<void(PresentationSizeQueue& queue)> bind_presentation_size_queue;
  // Optional storage wrapper (e.g. instrumented save stats).
  std::function<ae::IDomainStorage*(DirectoryDomainStorage& durable)>
      select_storage;
  std::function<void()> on_model_drained_accepted_tasks;
  std::function<void(ae::Obj& root)> on_clear_live_notifiers;
  std::function<void()> on_save_start;
  std::function<void(ae::Obj& root)> persist_root;
  ModelDurablePersistence durable_persistence{
      ModelDurablePersistence::PackedCheckpointFile};
  std::function<void(ae::Obj& root, ModelPublicationSaveContext const& save)>
      on_save_end;
  // Tests only: invoked on model thread immediately before incremental serialize.
  std::function<void(std::uint32_t dirty_node_id)> before_incremental_publication;
};

struct ModelPublicationSession {
  using ModelWork = ModelObjectProxy::ModelWork;

  ModelPublicationSessionConfig config;
  std::filesystem::path state_dir;
  PublicationChannel<3> channel;
  std::mutex mu;
  std::condition_variable cv;
  bool stop{false};
  int shutdown_exit_code{0};
  std::deque<ModelWork> pending_work;
  PresentationSizeQueue presentation_size_queue;
  // Keeps the model root alive for the whole Run() call (load_mutable_root
  // must not return the address of a temporary smart pointer).
  ae::Obj::ptr model_root;
  PendingDirtyNodes pending_dirty;
  std::atomic<std::size_t> pending_dirty_count_observed{0};
  std::atomic<bool> incremental_publication_in_progress{false};
  std::atomic<std::uint32_t> model_work_executions_since_incremental_publish{
      0};
  struct IncrementalPrepBarrier {
    std::mutex mu;
    std::condition_variable cv;
    bool enabled{false};
    bool at_checkpoint{false};
    bool add_posted{false};
    bool continue_prep{false};
  } incremental_prep_barrier;

  void RequestStop();
  void Post(ModelWork work);
  void Run(std::function<void(ModelPublicationKind)> on_published);
};

void ApplyModelStructuralPublication(std::vector<std::uint8_t> const& bytes,
                                     ae::Obj& ui_root,
                                     ae::IDomainStorage& ui_storage, void* host,
                                     ModelObjectProxy* model_proxy = nullptr);

}  // namespace apptraverse

#endif  // APPTRAVERSE_APPLICATION_MODEL_PUBLICATION_SESSION_H_
