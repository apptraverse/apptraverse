// Isolated native P2pSafeStream duplex regressions over scripted ByteIStream
// endpoints. No ChatWorkspace, SharedSyncRuntime, or GUI.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "aether/ae_context.h"
#include "aether/client_messages/p2p_safe_message_stream.h"
#include "aether/config.h"
#include "aether/safe_stream/safe_stream_config.h"
#include "aether/stream_api/istream.h"
#include "aether/types/data_buffer.h"
#include "aether/write_action/write_action.h"

#include "tests/test-safe-stream/stream-test-ctx.h"
#include "tests/test-stream/mock_write_stream.h"

#define CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"         \
                << __LINE__ << '\n';                                           \
      std::exit(1);                                                            \
    }                                                                          \
  } while (0)

namespace {

// Production chat adapter config (must stay aligned with chat_aether_runtime.cpp).
ae::SafeStreamConfig MakeProductionChatConfig() {
  return ae::SafeStreamConfig{
      .window_size = AE_SAFE_STREAM_CAPACITY / 2 - 1,
      .max_packet_size = AE_SAFE_STREAM_CAPACITY / 2 - 1,
      .max_repeat_count = 10,
      .wait_ack_timeout = std::chrono::seconds{5},
      .send_ack_timeout = std::chrono::milliseconds{1},
      .send_repeat_timeout = std::chrono::seconds{2},
  };
}

ae::SafeStreamConfig MakeConfig(std::chrono::milliseconds wait_ack =
                                    std::chrono::milliseconds{500}) {
  return ae::SafeStreamConfig{
      .window_size = 1024,
      .max_packet_size = 200,
      .max_repeat_count = 8,
      .wait_ack_timeout = wait_ack,
      .send_ack_timeout = std::chrono::milliseconds{0},
      .send_repeat_timeout = std::chrono::milliseconds{40},
  };
}

std::vector<std::uint8_t> MakePayload(std::size_t n, std::uint8_t tag) {
  std::vector<std::uint8_t> out(n);
  for (std::size_t i = 0; i < n; ++i) {
    out[i] = static_cast<std::uint8_t>((tag * 31u + i * 17u) & 0xffu);
  }
  if (n > 0) {
    out[0] = tag;
  }
  return out;
}

// Two MockWriteStreams cross-wired: opaque lower bytes only.
struct DuplexFixture {
  ae::TestContext ctx;
  std::shared_ptr<ae::MockWriteStream> pipe_a;
  std::shared_ptr<ae::MockWriteStream> pipe_b;
  std::unique_ptr<ae::P2pSafeStream> stream_a;
  std::unique_ptr<ae::P2pSafeStream> stream_b;
  std::vector<std::vector<std::uint8_t>> rx_a;
  std::vector<std::vector<std::uint8_t>> rx_b;
  ae::Subscription sub_a;
  ae::Subscription sub_b;
  ae::Subscription wire_a;
  ae::Subscription wire_b;
  ae::TimePoint epoch{ae::TimePoint::clock::now()};
  std::size_t tx_a{0};
  std::size_t tx_b{0};
  bool drop_next_a{false};
  bool drop_all_a{false};
  std::chrono::milliseconds delay_a{0};
  std::deque<ae::DataBuffer> held_a;

  explicit DuplexFixture(std::size_t link_limit = 64)
      : pipe_a{std::make_shared<ae::MockWriteStream>(ctx, link_limit)},
        pipe_b{std::make_shared<ae::MockWriteStream>(ctx, link_limit)} {
    // Make stream_info report Linked + writable like a live P2p port.
    // MockWriteStream sets sizes but leaves link_state default; patch via
    // WriteOut path after Tie by emitting update is not available, so we rely
    // on MockWriteStream's size fields and SafeStream OnStreamUpdate.
    wire_a = pipe_a->on_write_event().Subscribe([this](ae::DataBuffer&& data) {
      ++tx_a;
      if (drop_all_a) {
        return;
      }
      if (drop_next_a) {
        drop_next_a = false;
        return;
      }
      if (delay_a.count() > 0) {
        auto when = epoch + delay_a;
        auto packet = data;
        ctx.sched.DelayedTask(
            [this, packet = std::move(packet)]() mutable {
              pipe_b->WriteOut(packet);
            },
            when);
        return;
      }
      if (!held_a.empty() || held_a.size() == 0) {
        // optional reorder buffer unused unless held_a pre-armed
      }
      pipe_b->WriteOut(data);
    });
    wire_b = pipe_b->on_write_event().Subscribe([this](ae::DataBuffer&& data) {
      ++tx_b;
      pipe_a->WriteOut(data);
    });

    auto config = MakeConfig();
    stream_a = std::make_unique<ae::P2pSafeStream>(ctx, config, pipe_a);
    stream_b = std::make_unique<ae::P2pSafeStream>(ctx, config, pipe_b);
    sub_a = stream_a->out_data_event().Subscribe([this](ae::DataBuffer const& d) {
      rx_a.emplace_back(d.begin(), d.end());
    });
    sub_b = stream_b->out_data_event().Subscribe([this](ae::DataBuffer const& d) {
      rx_b.emplace_back(d.begin(), d.end());
    });
    Pump(4);
  }

  void Pump(int ticks = 1) {
    for (int i = 0; i < ticks; ++i) {
      epoch += std::chrono::milliseconds{5};
      ctx.Update(epoch);
    }
  }

  bool WaitRx(std::vector<std::vector<std::uint8_t>>& side, std::size_t n,
              int max_ticks = 800) {
    for (int i = 0; i < max_ticks && side.size() < n; ++i) {
      Pump(1);
    }
    return side.size() >= n;
  }

  std::size_t UsableMax() const {
    return stream_a->stream_info().max_element_size;
  }
};

void WatchWrite(ae::WriteAction& action, bool& done,
                ae::WriteAction::Status& st, ae::Subscription& sub) {
  // is_finished is not Success. If the action already finished before we can
  // subscribe, the terminal result is unavailable — fail the observation
  // instead of inventing kSuccess.
  if (action.is_finished()) {
    std::cerr << "WatchWrite: action already finished before Subscribe; "
                 "cannot invent Success\n";
    done = true;
    st = ae::WriteAction::Status::kFail;
    return;
  }
  sub = action.status_event().Subscribe([&](ae::WriteAction::Status s) {
    st = s;
    done = true;
  });
}

void PumpUntilDone(DuplexFixture& fx, bool& done, int max_ticks = 800) {
  for (int i = 0; i < max_ticks && !done; ++i) {
    fx.Pump(1);
  }
}

bool SendOne(DuplexFixture& fx, ae::P2pSafeStream& stream,
             std::vector<std::uint8_t> const& payload) {
  bool done = false;
  ae::WriteAction::Status st = ae::WriteAction::Status::kFail;
  ae::Subscription sub;
  ae::DataBuffer buf{payload.begin(), payload.end()};
  auto tx0 = fx.tx_a + fx.tx_b;
  auto& action = stream.Write(std::move(buf));
  WatchWrite(action, done, st, sub);
  PumpUntilDone(fx, done);
  std::cerr << "SendOne n=" << payload.size() << " done=" << done
            << " st=" << static_cast<int>(st) << " dtx=" << (fx.tx_a + fx.tx_b - tx0)
            << " tx_a=" << fx.tx_a << " tx_b=" << fx.tx_b
            << " rx_a=" << fx.rx_a.size() << " rx_b=" << fx.rx_b.size() << '\n';
  return done && st == ae::WriteAction::Status::kSuccess;
}

void ExpectPayload(std::vector<std::vector<std::uint8_t>> const& rx,
                   std::size_t index, std::vector<std::uint8_t> const& want) {
  CHECK(index < rx.size());
  CHECK(rx[index] == want);
}

void TestSequentialAndSizes() {
  DuplexFixture fx{48};
  auto const usable = fx.UsableMax();
  CHECK(usable > 0);
  std::cerr << "usable_max_element_size=" << usable
            << " link_state=" << static_cast<int>(fx.stream_a->stream_info().link_state)
            << " writable=" << fx.stream_a->stream_info().is_writable << '\n';

  // Sequential (not overlapping) writes on the small mock link fixture.
  std::vector<std::size_t> lengths{14, 24, 63, 64, 65, 224};
  for (std::size_t i = 0; i < lengths.size(); ++i) {
    auto payload = MakePayload(lengths[i], static_cast<std::uint8_t>(0xA0 + i));
    CHECK(SendOne(fx, *fx.stream_a, payload));
    CHECK(fx.WaitRx(fx.rx_b, i + 1));
    ExpectPayload(fx.rx_b, i, payload);
  }

  fx.rx_a.clear();
  std::vector<std::size_t> b_lengths{24, 224, 64, 65};
  for (std::size_t i = 0; i < b_lengths.size(); ++i) {
    auto payload =
        MakePayload(b_lengths[i], static_cast<std::uint8_t>(0xB0 + i));
    CHECK(SendOne(fx, *fx.stream_b, payload));
    CHECK(fx.WaitRx(fx.rx_a, i + 1));
    ExpectPayload(fx.rx_a, i, payload);
  }

  // Zero-length: not required to succeed on this SafeStream fixture.
  {
    DuplexFixture empty_fx{48};
    ae::DataBuffer buf;
    auto& action = empty_fx.stream_a->Write(std::move(buf));
    bool done = false;
    ae::WriteAction::Status st = ae::WriteAction::Status::kFail;
    ae::Subscription sub;
    WatchWrite(action, done, st, sub);
    PumpUntilDone(empty_fx, done, 200);
    std::cerr << "zero_length_write done=" << done
              << " st=" << static_cast<int>(st)
              << " finished=" << action.is_finished() << '\n';
  }
}

void TestProductionConfigRepresentativeSizes() {
  // Larger link so production max_packet_size can carry 830/840 samples.
  DuplexFixture fx{2048};
  fx.stream_a.reset();
  fx.stream_b.reset();
  auto config = MakeProductionChatConfig();
  fx.stream_a = std::make_unique<ae::P2pSafeStream>(fx.ctx, config, fx.pipe_a);
  fx.stream_b = std::make_unique<ae::P2pSafeStream>(fx.ctx, config, fx.pipe_b);
  fx.sub_a = fx.stream_a->out_data_event().Subscribe([&](ae::DataBuffer const& d) {
    fx.rx_a.emplace_back(d.begin(), d.end());
  });
  fx.sub_b = fx.stream_b->out_data_event().Subscribe([&](ae::DataBuffer const& d) {
    fx.rx_b.emplace_back(d.begin(), d.end());
  });
  fx.Pump(4);

  auto const usable = fx.UsableMax();
  std::cerr << "production_usable_max_element_size=" << usable << '\n';
  std::vector<std::size_t> const required{24, 224, 830, 840};
  for (std::size_t i = 0; i < required.size(); ++i) {
    auto const n = required[i];
    if (usable < n) {
      std::cerr << "UNSUPPORTED production size=" << n
                << " usable_max=" << usable
                << " (not substituting a smaller sample)\n";
      std::exit(1);
    }
    auto payload = MakePayload(n, static_cast<std::uint8_t>(0xC0 + i));
    CHECK(SendOne(fx, *fx.stream_a, payload));
    CHECK(fx.WaitRx(fx.rx_b, i + 1, 2000));
    ExpectPayload(fx.rx_b, i, payload);
  }
}

void TestSimultaneousPending() {
  DuplexFixture fx{40};
  auto pa = MakePayload(90, 0x11);
  auto pb = MakePayload(110, 0x22);
  bool done_a = false;
  bool done_b = false;
  ae::WriteAction::Status sta = ae::WriteAction::Status::kFail;
  ae::WriteAction::Status stb = ae::WriteAction::Status::kFail;
  ae::Subscription sa;
  ae::Subscription sb;
  ae::DataBuffer ba{pa.begin(), pa.end()};
  ae::DataBuffer bb{pb.begin(), pb.end()};
  auto& wa = fx.stream_a->Write(std::move(ba));
  auto& wb = fx.stream_b->Write(std::move(bb));
  WatchWrite(wa, done_a, sta, sa);
  WatchWrite(wb, done_b, stb, sb);
  for (int i = 0; i < 800 && !(done_a && done_b); ++i) {
    fx.Pump(1);
  }
  CHECK(done_a && done_b);
  CHECK(sta == ae::WriteAction::Status::kSuccess);
  CHECK(stb == ae::WriteAction::Status::kSuccess);
  CHECK(fx.WaitRx(fx.rx_b, 1));
  CHECK(fx.WaitRx(fx.rx_a, 1));
  ExpectPayload(fx.rx_b, 0, pa);
  ExpectPayload(fx.rx_a, 0, pb);
}

void TestSmallLargeBothDirections() {
  // Covered by TestSequentialAndSizes (24/224 both directions on one pair).
}

void TestSequentialWritesSettleCorrectOps() {
  // Sequential back-to-back Writes — not genuinely overlapping ops.
  DuplexFixture fx{40};
  std::vector<std::vector<std::uint8_t>> payloads{
      MakePayload(20, 1), MakePayload(30, 2), MakePayload(40, 3)};
  for (std::size_t i = 0; i < payloads.size(); ++i) {
    CHECK(SendOne(fx, *fx.stream_a, payloads[i]));
  }
  CHECK(fx.WaitRx(fx.rx_b, payloads.size()));
  for (std::size_t i = 0; i < payloads.size(); ++i) {
    ExpectPayload(fx.rx_b, i, payloads[i]);
  }
}

void TestDelayedDeliveryBeyondThreeSecondsNativePolicy() {
  DuplexFixture fx{64};
  fx.delay_a = std::chrono::milliseconds{3500};
  // Rebuild streams with longer wait_ack.
  fx.stream_a.reset();
  fx.stream_b.reset();
  auto config = MakeConfig(std::chrono::milliseconds{5000});
  fx.stream_a = std::make_unique<ae::P2pSafeStream>(fx.ctx, config, fx.pipe_a);
  fx.stream_b = std::make_unique<ae::P2pSafeStream>(fx.ctx, config, fx.pipe_b);
  fx.sub_a = fx.stream_a->out_data_event().Subscribe([&](ae::DataBuffer const& d) {
    fx.rx_a.emplace_back(d.begin(), d.end());
  });
  fx.sub_b = fx.stream_b->out_data_event().Subscribe([&](ae::DataBuffer const& d) {
    fx.rx_b.emplace_back(d.begin(), d.end());
  });
  fx.Pump(2);

  auto const start = fx.epoch;
  auto payload = MakePayload(48, 0x77);
  CHECK(SendOne(fx, *fx.stream_a, payload));
  CHECK(fx.WaitRx(fx.rx_b, 1, 1200));
  ExpectPayload(fx.rx_b, 0, payload);
  auto const elapsed = fx.epoch - start;
  CHECK(elapsed >= std::chrono::milliseconds{3500});
  std::cerr << "delayed_delivery_simulated_ms="
            << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed)
                   .count()
            << '\n';
}

void TestInFlightStopSettlesWriteAction() {
  // Regression: SafeStreamSendAction::Stop used to no-op while a chunk was
  // still listed as in-flight, and stopped_event_ was never subscribed — so
  // WriteAction::Stop never produced a terminal status.
  DuplexFixture fx{48};
  fx.drop_all_a = true;
  auto payload = MakePayload(40, 0x42);
  ae::DataBuffer buf{payload.begin(), payload.end()};
  bool done = false;
  ae::WriteAction::Status st = ae::WriteAction::Status::kFail;
  ae::Subscription sub;
  auto& action = fx.stream_a->Write(std::move(buf));
  WatchWrite(action, done, st, sub);
  fx.Pump(20);
  CHECK(!done);
  CHECK(!action.is_finished());
  action.Stop();
  PumpUntilDone(fx, done, 200);
  CHECK(done);
  CHECK(st == ae::WriteAction::Status::kStop);
  CHECK(action.is_finished());
}

void TestDropRecover() {
  DuplexFixture fx{48};
  fx.drop_next_a = true;
  auto payload = MakePayload(70, 0x55);
  CHECK(SendOne(fx, *fx.stream_a, payload));
  CHECK(fx.WaitRx(fx.rx_b, 1, 800));
  ExpectPayload(fx.rx_b, 0, payload);
}

void TestFragmentedMessage() {
  DuplexFixture fx{32};
  auto usable = fx.UsableMax();
  CHECK(usable > 0);
  auto payload = MakePayload(std::min(usable, std::size_t{180}), 0x99);
  CHECK(payload.size() > 32);
  auto tx_before = fx.tx_a;
  CHECK(SendOne(fx, *fx.stream_a, payload));
  CHECK(fx.WaitRx(fx.rx_b, 1, 800));
  ExpectPayload(fx.rx_b, 0, payload);
  CHECK(fx.tx_a > tx_before + 1);
}

void TestReentrancyDepthProperty() {
  DuplexFixture fx{48};
  fx.delay_a = std::chrono::milliseconds{5};

  int callback_depth = 0;
  int max_write_depth = 0;
  std::deque<std::vector<std::uint8_t>> pending{
      MakePayload(20, 1), MakePayload(20, 2), MakePayload(20, 3)};
  bool write_in_flight = false;
  std::vector<ae::Subscription> subs;

  std::function<void()> flush_reentrant;
  flush_reentrant = [&]() {
    if (write_in_flight || pending.empty()) {
      return;
    }
    auto payload = std::move(pending.front());
    pending.pop_front();
    write_in_flight = true;
    if (callback_depth > max_write_depth) {
      max_write_depth = callback_depth;
    }
    ae::DataBuffer buf{payload.begin(), payload.end()};
    auto& action = fx.stream_a->Write(std::move(buf));
    CHECK(!action.is_finished());
    subs.push_back(action.status_event().Subscribe([&](ae::WriteAction::Status) {
      ++callback_depth;
      write_in_flight = false;
      flush_reentrant();
      --callback_depth;
    }));
  };

  flush_reentrant();
  for (int i = 0; i < 800 && (!pending.empty() || write_in_flight); ++i) {
    fx.Pump(1);
  }
  CHECK(pending.empty());
  CHECK(!write_in_flight);
  CHECK(max_write_depth >= 1);
  std::cerr << "reentrant_write_max_callback_depth=" << max_write_depth << '\n';

  DuplexFixture fx2{48};
  fx2.delay_a = std::chrono::milliseconds{5};
  callback_depth = 0;
  max_write_depth = 0;
  pending = {MakePayload(20, 4), MakePayload(20, 5), MakePayload(20, 6)};
  write_in_flight = false;
  bool terminal_pending = false;
  ae::WriteAction::Status terminal_status = ae::WriteAction::Status::kFail;
  ae::Subscription active_sub;
  std::function<void()> start_outer;
  start_outer = [&]() {
    if (write_in_flight || pending.empty()) {
      return;
    }
    auto payload = std::move(pending.front());
    pending.pop_front();
    write_in_flight = true;
    if (callback_depth > max_write_depth) {
      max_write_depth = callback_depth;
    }
    ae::DataBuffer buf{payload.begin(), payload.end()};
    auto& action = fx2.stream_a->Write(std::move(buf));
    CHECK(!action.is_finished());
    active_sub = action.status_event().Subscribe([&](ae::WriteAction::Status s) {
      ++callback_depth;
      terminal_status = s;
      terminal_pending = true;
      --callback_depth;
    });
  };

  start_outer();
  for (int i = 0; i < 800 && (!pending.empty() || write_in_flight); ++i) {
    fx2.Pump(1);
    if (terminal_pending) {
      terminal_pending = false;
      active_sub.Reset();
      write_in_flight = false;
      CHECK(terminal_status == ae::WriteAction::Status::kSuccess);
      start_outer();
    }
  }
  CHECK(pending.empty());
  CHECK(!write_in_flight);
  CHECK(max_write_depth == 0);
  std::cerr << "outer_pump_write_max_callback_depth=" << max_write_depth << '\n';
}


void TestLoopbackSmoke() {
  ae::TestContext ctx;
  auto pipe = std::make_shared<ae::MockWriteStream>(ctx, 120);
  auto wire = pipe->on_write_event().Subscribe([&](ae::DataBuffer&& data) {
    pipe->WriteOut(data);
  });
  auto stream = std::make_unique<ae::P2pSafeStream>(ctx, MakeConfig(), pipe);
  std::vector<std::uint8_t> rx;
  auto sub = stream->out_data_event().Subscribe([&](ae::DataBuffer const& d) {
    rx.assign(d.begin(), d.end());
  });
  auto epoch = ae::TimePoint::clock::now();
  for (int i = 0; i < 4; ++i) { epoch += std::chrono::milliseconds{5}; ctx.Update(epoch); }
  auto payload = MakePayload(14, 0xAB);
  bool done = false;
  ae::WriteAction::Status st = ae::WriteAction::Status::kFail;
  ae::Subscription wsub;
  ae::DataBuffer buf{payload.begin(), payload.end()};
  auto& action = stream->Write(std::move(buf));
  WatchWrite(action, done, st, wsub);
  for (int i = 0; i < 200 && !done; ++i) { epoch += std::chrono::milliseconds{5}; ctx.Update(epoch); }
  std::cerr << "LOOPBACK_SMOKE done=" << done << " st=" << (int)st << " rx=" << rx.size()
            << " max=" << stream->stream_info().max_element_size
            << " link=" << (int)stream->stream_info().link_state << "\n";
  CHECK(done);
  CHECK(rx == payload);
}

}  // namespace

int main() {
  std::cerr << "aether_p2p_safe_stream_duplex_test start\n";
  TestLoopbackSmoke();
  TestSequentialAndSizes();
  TestProductionConfigRepresentativeSizes();
  TestSimultaneousPending();
  TestSmallLargeBothDirections();
  TestSequentialWritesSettleCorrectOps();
  TestDelayedDeliveryBeyondThreeSecondsNativePolicy();
  TestInFlightStopSettlesWriteAction();
  TestDropRecover();
  TestFragmentedMessage();
  TestReentrancyDepthProperty();
  std::cerr << "aether_p2p_safe_stream_duplex_test PASS\n";
  return 0;
}
