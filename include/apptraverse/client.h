#ifndef APPTRAVERSE_CLIENT_H_
#define APPTRAVERSE_CLIENT_H_

#include <string>

#include "apptraverse/node_for.h"

namespace apptraverse {

class Client : public NodeFor<Client> {
  AE_OBJECT(Client, Node, 0)

 protected:
  Client() = default;

 public:
  explicit Client(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(name))

  std::string name;
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_CLIENT_H_
