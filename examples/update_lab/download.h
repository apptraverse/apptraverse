#pragma once
#include "model.h"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
namespace update_lab {
struct TransferRequest {
  DownloadSnapshot snapshot;
  unsigned current_release, current_executable, bytes_per_second;
};
struct TransferResult {
  std::uint64_t epoch;
  Intent intent;
  Catalog catalog;
  Bytes supply, language, executable;
  unsigned executable_version;
  std::uint64_t resumed_bytes;
  bool used_delta;
};
class TransferWorker {
 public:
  using Report = std::function<bool(DownloadSnapshot)>;
  using Ready = std::function<void(TransferResult)>;
  TransferWorker(fs::path profile, fs::path local_source, std::string provider, Report, Ready);
  ~TransferWorker();
  void Begin(TransferRequest);
  void Stop();
 private:
  void Run();
  void Transfer(TransferRequest);
  fs::path profile_, local_source_;
  std::string provider_;
  Report report_;
  Ready ready_;
  std::atomic<bool> stop_{false};
  std::atomic<std::uint64_t> wanted_{0};
  std::mutex mutex_;
  std::condition_variable condition_;
  std::unique_ptr<TransferRequest> pending_;
  std::thread thread_;
};
bool AppendDurable(fs::path const&, Bytes const&);
}
