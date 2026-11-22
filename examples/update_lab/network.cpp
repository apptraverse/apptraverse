#include "network.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <deque>
#include <fstream>
#include <map>
#include <optional>
#include "aether/aether_app.h"
#include "aether/ae_actions/select_client.h"
#include "aether/client_messages/p2p_message_stream.h"
#include "aether/client_messages/p2p_stream_write_budget.h"
#include "apptraverse/directory_domain_storage.h"
namespace update_lab {
// SDK cloud messages with offset-addressed pieces, without SharedNode or digests.
struct AetherPeer::Impl {
  struct Peer {
    explicit Peer(ae::Uid value) : uid{value} {}
    ae::Uid uid;
    std::unique_ptr<ae::P2pStream> stream;
    ae::Subscription read_subscription, write_subscription;
    struct Job { std::uint64_t request; Bytes bytes; };
    std::deque<Job> outgoing;
    FrameAssembler incoming;
    std::size_t offset{0}, submitted{0};
    std::uint64_t sent{0}, received{0};
    bool writing{false};
    std::optional<ae::WriteAction::Status> terminal;
    ~Peer() { write_subscription.Reset(); read_subscription.Reset(); stream.reset(); }
  };
  fs::path identity_profile;
  std::unique_ptr<ae::AetherApp> app;
  ae::Client::ptr client;
  ae::Subscription selection, ports;
  std::map<std::string, std::unique_ptr<Peer>> peers;
  std::function<void(std::string const&, Frame)> receiver;
  bool failed{false}, selected{false};
  std::string error;
  Impl(fs::path profile, std::string name) : identity_profile{std::move(profile)} {
    app = ae::AetherApp::Construct(ae::AetherAppContext{[this]() -> std::unique_ptr<ae::IDomainStorage> {
      return std::make_unique<apptraverse::DirectoryDomainStorage>(identity_profile);
    }});
    if (!app) { failed = true; return; }
    selection = app->aether()->SelectClient(
        ae::Uid::FromString("3ac93165-3d37-4970-87a6-fa4ee27744e4"), name)
        .result_event().Subscribe([this](auto const& result) {
          if (!result) { failed = true; return; }
          client = result.value();
        });
  }
  ~Impl() { ports.Reset(); peers.clear(); selection.Reset(); client = {}; app.reset(); }
  Peer& Get(std::string const& text) {
    auto found = peers.find(text);
    if (found == peers.end()) {
      auto uid = ae::Uid::FromString(ae::UidString{std::string_view{text}});
      found = peers.emplace(text, std::make_unique<Peer>(uid)).first;
    }
    return *found->second;
  }
  void Bind(Peer& peer, ae::P2pPortHandle handle) {
    peer.write_subscription.Reset(); peer.read_subscription.Reset(); peer.stream.reset();
    peer.writing = false; peer.terminal.reset(); peer.offset = 0; peer.incoming.Clear();
    ae::StreamRoutePolicy routing;
    routing.lan = false; routing.direct_ip = false; routing.hole_punching = false;
    routing.cloud = true; routing.keep_backup_channels = true;
    peer.stream = std::make_unique<ae::P2pStream>(*app->aether(), client.Load(), peer.uid,
        std::move(handle), ae::RequestPolicy::MainServer{}, routing);
    peer.read_subscription = peer.stream->out_data_event().Subscribe([this, &peer](ae::DataBuffer const& chunk) {
      peer.received += chunk.size();
      FramePiece piece;
      if (!Decode(chunk, piece)) { failed = true; error = "native FramePiece decode"; return; }
      auto frame = peer.incoming.Receive(piece);
      if (frame && receiver) receiver(ae::Format("{}", peer.uid), std::move(*frame));
    });
  }
  void Pump(Peer& peer) {
    if (!peer.stream) Bind(peer, client->message_stream_manager().CreatePort(peer.uid));
    if (peer.terminal) {
      bool success = *peer.terminal == ae::WriteAction::Status::kSuccess;
      peer.write_subscription.Reset(); peer.terminal.reset(); peer.writing = false;
      if (!success) { failed = true; error = "SDK write completion"; return; }
      peer.offset += peer.submitted;
      peer.sent += peer.submitted;
      if (peer.offset == peer.outgoing.front().bytes.size()) { peer.outgoing.pop_front(); peer.offset = 0; }
    }
    if (peer.writing || peer.outgoing.empty()) return;
    auto info = peer.stream->stream_info();
    if (!info.is_writable || !info.max_element_size) return;
    auto budget = ae::P2pStreamWritePayloadLimitFromChannelMax(info.max_element_size);
    auto const& job = peer.outgoing.front();
    if (budget <= 20) return;
    peer.submitted = std::min<std::size_t>({budget - 20, 1024, job.bytes.size() - peer.offset});
    if (!peer.submitted) return;
    FramePiece piece{job.request, static_cast<std::uint32_t>(job.bytes.size()),
        static_cast<std::uint32_t>(peer.offset),
        Bytes{job.bytes.begin() + peer.offset, job.bytes.begin() + peer.offset + peer.submitted}};
    auto part = Encode(piece);
    peer.writing = true;
    auto& action = peer.stream->Write(std::move(part));
    if (action.is_finished()) { failed = true; error = "SDK synchronous write failure"; return; }
    peer.write_subscription = action.status_event().Subscribe([&peer](ae::WriteAction::Status status) { peer.terminal = status; });
  }
};
AetherPeer::AetherPeer(fs::path p, std::string name) : impl_{std::make_unique<Impl>(std::move(p), std::move(name))} {}
AetherPeer::~AetherPeer() = default;
void AetherPeer::Tick() {
  auto& i = *impl_;
  if (i.failed) return;
  i.app->Update(ae::Now());
  if (i.client.is_valid() && !i.selected) {
    i.client->connectivity_policy().Load()->ConfigureRxTimings().ForAllPriorities(
        ae::RxTimingConf::Every(std::chrono::milliseconds{1000}));
    i.app->aether().Save();
    i.ports = i.client->message_stream_manager().new_port_event().Subscribe([&i](ae::P2pPortHandle& handle) {
      if (!handle || handle.stream_id() != 0 || handle.service() != ae::PeerService{}) return;
      auto& peer = i.Get(ae::Format("{}", handle.destination()));
      i.Bind(peer, std::move(handle));
    });
    i.selected = true;
  }
  if (!i.selected) return;
  for (auto& [id, peer] : i.peers) i.Pump(*peer);
}
std::string AetherPeer::Endpoint() const { return impl_->selected ? ae::Format("{}", impl_->client->uid()) : std::string{}; }
void AetherPeer::Send(std::string to, Frame frame) {
  auto& queue = impl_->Get(to).outgoing;
  for (auto const& job : queue) if (job.request == frame.request) return;
  queue.push_back({frame.request, Encode(frame)});
}
void AetherPeer::Receive(std::function<void(std::string const&, Frame)> r) { impl_->receiver = std::move(r); }
bool AetherPeer::failed() const { return impl_->failed; }
std::string AetherPeer::Diagnostics() const {
  auto const& i = *impl_;
  std::string result = i.error + " selected=" + std::to_string(i.selected);
  for (auto const& [id, peer] : i.peers)
    result += " peer=" + id + " tx=" + std::to_string(peer->sent) + " rx=" + std::to_string(peer->received) +
        " queued=" + std::to_string(peer->outgoing.size()) + " writing=" + std::to_string(peer->writing) +
        " buffered=" + std::to_string(peer->incoming.buffered());
  return result;
}
Frame Serve(fs::path const& store, Frame const& request) {
  Frame reply{1, 5, request.id, request.chunk, {}};
  reply.request = request.request;
  if (request.kind == 1) {
    if (ReadFile(store / "catalog.bin", reply.payload)) reply.kind = 3;
    return reply;
  }
  if (request.kind != 2) return reply;
  Catalog catalog; Bytes bytes;
  if (!ReadFile(store / "catalog.bin", bytes) || !ReadCatalog(bytes, catalog)) return reply;
  auto const* component = FindComponent(catalog, request.id);
  if (!component || request.chunk * kChunkSize >= component->size) return reply;
  std::ifstream file{store / "components" / FileId(component->id), std::ios::binary};
  auto size = std::min(kChunkSize, component->size - request.chunk * kChunkSize);
  reply.payload.resize(static_cast<std::size_t>(size));
  file.seekg(static_cast<std::streamoff>(request.chunk * kChunkSize));
  file.read(reinterpret_cast<char*>(reply.payload.data()), static_cast<std::streamsize>(size));
  if (file) reply.kind = 4;
  return reply;
}
}
