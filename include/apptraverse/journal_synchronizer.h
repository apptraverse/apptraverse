#ifndef APPTRAVERSE_JOURNAL_SYNCHRONIZER_H_
#define APPTRAVERSE_JOURNAL_SYNCHRONIZER_H_

#include "aether/obj/domain.h"
#include "aether/obj/idomain_storage.h"

#include "apptraverse/event_transport.h"
#include "apptraverse/node.h"

namespace apptraverse {

/**
 * \brief Orchestrates journal exchange for one Node over one transport endpoint.
 *
 * Node does not own or call transport. The application/test calls FlushPending
 * after creating local events.
 */
class JournalSynchronizer final : public IEventTransportReceiver {
 public:
  JournalSynchronizer(Node& node, ae::Domain& domain,
                      ae::IDomainStorage& storage, IEventTransport& transport);

  void FlushPending();

  void OnEvent(EventTransportMessage message) override;
  void OnConfirmation(EventConfirmation confirmation) override;

 private:
  Node* node_;
  ae::Domain* domain_;
  ae::IDomainStorage* storage_;
  IEventTransport* transport_;
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_JOURNAL_SYNCHRONIZER_H_
