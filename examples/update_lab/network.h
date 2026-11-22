#pragma once
#include "wire.h"
#include <functional>
#include <memory>
namespace update_lab {
class AetherPeer {
 public:
  explicit AetherPeer(fs::path profile, std::string name);
  ~AetherPeer();
  void Tick();
  std::string Endpoint() const;
  void Send(std::string destination, Frame frame);
  void Receive(std::function<void(std::string const&, Frame)>);
  bool failed() const;
  std::string Diagnostics() const;
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
Frame Serve(fs::path const& store, Frame const& request);
}
