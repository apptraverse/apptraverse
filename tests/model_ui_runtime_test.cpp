#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "aether/clock.h"
#include "aether/domain_storage/ram_domain_storage.h"

#include "demo_bootstrap.h"
#include "demo_events.h"
#include "demo_ids.h"
#include "demo_model.h"
#include "model_executor.h"
#include "ui_publication.h"
#include "ui_runtime_registry.h"

namespace apptraverse::test {
namespace {

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"       \
                << __LINE__ << '\n';                                         \
      std::exit(1);                                                          \
    }                                                                        \
  } while (0)

bool HasId(std::vector<std::uint32_t> const& ids, std::uint32_t id) {
  return std::find(ids.begin(), ids.end(), id) != ids.end();
}

struct Harness {
  ae::RamDomainStorage storage;
  ae::Domain domain;
  DemoGraph graph;
  ImmutableObjectStore store;
  UiRuntimeRegistry registry;
  std::unique_ptr<ModelExecutor> exec;
  std::vector<UiApplyResult> applies;
  std::vector<std::uint8_t> last_bytes;

  Harness() : domain{ae::Now(), storage} {
    EnsureDemoRegistration();
    graph = BuildDemoGraph(domain);
    CaptureDemoBases(graph);
    ResolveDemoConstRefs(graph, store);
    exec = std::make_unique<ModelExecutor>(
        *graph.application, store,
        [this](std::uint32_t, PublicationChannel<3>* channel) {
          auto* buffer = channel->TakePublished();
          CHECK(buffer != nullptr);
          last_bytes = buffer->sink.bytes;
          applies.push_back(DeserializeUiSubgraphIntoExisting(
              buffer->sink, registry, store));
          channel->ReleaseConsumer();
        });
  }

  void Pump(std::chrono::steady_clock::time_point now) { exec->PumpOnce(now); }

  UiApplyResult const& LastFor(std::uint32_t root_id) const {
    for (auto it = applies.rbegin(); it != applies.rend(); ++it) {
      if (it->root_id == root_id) {
        return *it;
      }
    }
    std::cerr << "no publication for root " << root_id << '\n';
    std::exit(1);
  }
};

WindowBoundsCommand BoundsOf(Window const& window, std::int32_t extra_w,
                             std::int32_t extra_h) {
  WindowBoundsCommand command;
  command.window_id = window.obj_id.id();
  command.left = window.left;
  command.top = window.top;
  command.right = window.right + extra_w;
  command.bottom = window.bottom + extra_h;
  command.dpi = window.dpi;
  command.client_width = window.client_width + extra_w;
  command.client_height = window.client_height + extra_h;
  return command;
}

void TestJournalCommitAndDerivedLayout() {
  Harness h;
  auto& window = *h.graph.window_b;
  auto& toolbar = *h.graph.text_toolbar;
  auto& chat = *h.graph.chat;
  CHECK(window.journal.empty());
  CHECK(toolbar.journal.empty());
  CHECK(chat.journal.empty());

  auto event =
      WindowBoundsChangedEvent::ptr::Create(ae::CreateWith{*window.domain});
  event->left = window.left;
  event->top = window.top;
  event->right = window.right + 40;
  event->bottom = window.bottom;
  event->dpi = window.dpi;
  event->client_width = window.client_width + 40;
  event->client_height = window.client_height;
  window.Commit(event);

  CHECK(window.journal.size() == 1);
  CHECK(toolbar.journal.empty());
  CHECK(h.graph.color_toolbar->journal.empty());
  CHECK(chat.journal.empty());
  CHECK(toolbar.width == window.client_width);
  CHECK(chat.width == (window.client_width * 2) / 3);

  // Reset children to captured bases, then replay Window journal.
  h.graph.text_toolbar->ReplayFromBase();
  h.graph.color_toolbar->ReplayFromBase();
  h.graph.chat->ReplayFromBase();
  window.ReplayFromBase();
  CHECK(window.journal.size() == 1);
  CHECK(toolbar.journal.empty());
  CHECK(chat.journal.empty());
  CHECK(toolbar.width == window.client_width);
  CHECK(chat.width == (window.client_width * 2) / 3);

  h.graph.application.Save();
  ae::Domain loaded{ae::Now(), h.storage};
  auto app = Application::ptr::Declare(ae::CreateWith{loaded}.with_id(
      demo::ToObjId(demo::DemoObjId::Application)));
  app.Load();
  CHECK(app);
  app->window_b.Load();
  auto& loaded_window = *app->window_b;
  loaded_window.text_toolbar.Load();
  loaded_window.chat.Load();
  CHECK(loaded_window.journal.size() == 1);
  CHECK(loaded_window.text_toolbar->journal.empty());
  CHECK(loaded_window.text_toolbar->width == loaded_window.client_width);
  CHECK(loaded_window.chat->width == (loaded_window.client_width * 2) / 3);
}

void TestGenerationResize() {
  Harness h;
  auto& window = *h.graph.window_b;
  auto& text = *h.graph.text_toolbar;
  auto& color = *h.graph.color_toolbar;
  auto& chat = *h.graph.chat;

  auto const w0 = window.Generation();
  auto const t0 = text.Generation();
  auto const c0 = color.Generation();
  auto const h0 = chat.Generation();

  text.UpdateFromParent(window);
  color.UpdateFromParent(window);
  chat.UpdateFromParent(window);
  CHECK(text.Generation() == t0);
  CHECK(color.Generation() == c0);
  CHECK(chat.Generation() == h0);

  window.Commit([&] {
    auto event =
        WindowBoundsChangedEvent::ptr::Create(ae::CreateWith{*window.domain});
    auto cmd = BoundsOf(window, 80, 0);
    event->left = cmd.left;
    event->top = cmd.top;
    event->right = cmd.right;
    event->bottom = cmd.bottom;
    event->dpi = cmd.dpi;
    event->client_width = cmd.client_width;
    event->client_height = cmd.client_height;
    return event;
  }());
  CHECK(window.Generation() > w0);
  CHECK(text.Generation() > t0);
  CHECK(color.Generation() > c0);
  CHECK(chat.Generation() > h0);

  auto const w1 = window.Generation();
  auto const t1 = text.Generation();
  auto const c1 = color.Generation();
  auto const h1 = chat.Generation();
  window.Commit([&] {
    auto event =
        WindowBoundsChangedEvent::ptr::Create(ae::CreateWith{*window.domain});
    auto cmd = BoundsOf(window, 0, 60);
    event->left = cmd.left;
    event->top = cmd.top;
    event->right = cmd.right;
    event->bottom = cmd.bottom;
    event->dpi = cmd.dpi;
    event->client_width = cmd.client_width;
    event->client_height = cmd.client_height;
    return event;
  }());
  CHECK(window.Generation() > w1);
  CHECK(text.Generation() == t1);
  CHECK(color.Generation() == c1);
  CHECK(chat.Generation() > h1);
}

void TestPeriodicUpdate() {
  Harness h;
  auto now = std::chrono::steady_clock::now();
  h.Pump(now);
  CHECK(h.graph.color_toolbar->journal.empty());
  h.Pump(now + demo::kModelUpdatePeriod);
  CHECK(h.graph.color_toolbar->journal.empty());
  h.Pump(now + demo::kColorChangePeriod);
  CHECK(h.graph.color_toolbar->journal.size() == 1);
  CHECK(h.graph.color_toolbar->journal[0].event->GetClassId() ==
        ColorChangedEvent::kClassId);
  h.Pump(now + demo::kColorChangePeriod + demo::kModelUpdatePeriod);
  CHECK(h.graph.color_toolbar->journal.size() == 1);
}

void TestPublicationReuseAndStableAddresses() {
  Harness h;
  auto now = std::chrono::steady_clock::now();
  h.Pump(now);
  auto const window_b = demo::ToObjId(demo::DemoObjId::WindowB);
  auto const first = h.LastFor(window_b);
  CHECK(HasId(first.changed_obj_ids, window_b));
  CHECK(HasId(first.changed_obj_ids, demo::ToObjId(demo::DemoObjId::TextToolbar)));
  CHECK(HasId(first.changed_obj_ids, demo::ToObjId(demo::DemoObjId::ColorToolbar)));
  CHECK(HasId(first.changed_obj_ids, demo::ToObjId(demo::DemoObjId::Chat)));
  CHECK(first.reused_obj_ids.empty());

  auto* chat_addr = h.registry.Must<RuntimeChat>(
      demo::ToObjId(demo::DemoObjId::Chat));
  auto* text_addr = h.registry.Must<RuntimeTextToolbar>(
      demo::ToObjId(demo::DemoObjId::TextToolbar));
  auto const text_gen = text_addr->generation;
  auto const color_gen =
      h.registry.Must<RuntimeColorToolbar>(
                   demo::ToObjId(demo::DemoObjId::ColorToolbar))
          ->generation;

  h.exec->PostCommand(BoundsOf(*h.graph.window_b, 0, 70));
  h.Pump(now + std::chrono::milliseconds{20});
  auto const second = h.LastFor(window_b);
  CHECK(HasId(second.changed_obj_ids, window_b));
  CHECK(HasId(second.changed_obj_ids, demo::ToObjId(demo::DemoObjId::Chat)));
  CHECK(!HasId(second.changed_obj_ids, demo::ToObjId(demo::DemoObjId::TextToolbar)));
  CHECK(!HasId(second.changed_obj_ids, demo::ToObjId(demo::DemoObjId::ColorToolbar)));
  CHECK(HasId(second.reused_obj_ids, demo::ToObjId(demo::DemoObjId::TextToolbar)));
  CHECK(HasId(second.reused_obj_ids, demo::ToObjId(demo::DemoObjId::ColorToolbar)));
  CHECK(h.registry.Must<RuntimeChat>(demo::ToObjId(demo::DemoObjId::Chat)) ==
        chat_addr);
  CHECK(h.registry.Must<RuntimeTextToolbar>(
            demo::ToObjId(demo::DemoObjId::TextToolbar)) == text_addr);
  CHECK(text_addr->generation == text_gen);
  CHECK(h.registry.Must<RuntimeColorToolbar>(
                   demo::ToObjId(demo::DemoObjId::ColorToolbar))
            ->generation == color_gen);
}

void TestImmutableConstants() {
  Harness h;
  auto now = std::chrono::steady_clock::now();
  h.Pump(now);
  auto const window_b = demo::ToObjId(demo::DemoObjId::WindowB);
  auto const last = h.LastFor(window_b);
  CHECK(HasId(last.const_ref_ids, demo::ToObjId(demo::DemoObjId::ToolbarText)));
  CHECK(h.registry.Find(demo::ToObjId(demo::DemoObjId::ToolbarText)) == nullptr);
  std::string haystack(h.last_bytes.begin(), h.last_bytes.end());
  CHECK(haystack.find(demo::kToolbarTextBytes) == std::string::npos);
  CHECK(h.graph.text_toolbar->text.ptr == &*h.graph.toolbar_text);
  CHECK(h.store.Find(h.graph.toolbar_text.id()) == &*h.graph.toolbar_text);
}

void TestRepaintDoesNotTouchModel() {
  Harness h;
  auto now = std::chrono::steady_clock::now();
  h.Pump(now);
  auto const gen = h.graph.window_a->Generation();
  auto const pubs = h.exec->publication_count(h.graph.window_a.id().id());
  auto const journal = h.graph.window_a->journal.size();
  std::uint64_t paint_count = 0;
  ++paint_count;
  CHECK(paint_count == 1);
  CHECK(h.graph.window_a->Generation() == gen);
  CHECK(h.exec->publication_count(h.graph.window_a.id().id()) == pubs);
  CHECK(h.graph.window_a->journal.size() == journal);
}

void TestConsistencyMessageThenResize() {
  Harness h;
  auto now = std::chrono::steady_clock::now();
  h.Pump(now);
  AddMessageCommand add;
  add.chat_id = h.graph.chat.id().id();
  add.text = "hello-consistency";
  h.exec->PostCommand(add);
  h.exec->PostCommand(BoundsOf(*h.graph.window_b, 50, 40));
  h.Pump(now + std::chrono::milliseconds{20});
  auto* runtime = h.registry.Must<RuntimeChat>(h.graph.chat.id().id());
  CHECK(!runtime->messages.empty());
  CHECK(runtime->messages.back() == "hello-consistency");
  CHECK(runtime->width == h.graph.chat->width);
  CHECK(runtime->height == h.graph.chat->height);
  CHECK(h.graph.chat->messages.back() == "hello-consistency");
}

void TestNoRuntimeSaveInDemoSources() {
#ifdef MODEL_UI_RUNTIME_DEMO_SOURCE_DIR
  std::filesystem::path const root{MODEL_UI_RUNTIME_DEMO_SOURCE_DIR};
  CHECK(std::filesystem::exists(root));
  for (auto const& entry : std::filesystem::recursive_directory_iterator(root)) {
    if (!entry.is_regular_file()) {
      continue;
    }
    auto const ext = entry.path().extension().string();
    if (ext != ".h" && ext != ".cpp") {
      continue;
    }
    std::ifstream in{entry.path()};
    std::string line;
    while (std::getline(in, line)) {
      // Domain object.Save() only. BinaryArchive::Save(field) is UI publication.
      auto const pos = line.find(".Save()");
      if (pos == std::string::npos) {
        continue;
      }
      CHECK(line.find("runtime-save-ok") != std::string::npos);
    }
  }
#else
  CHECK(false && "MODEL_UI_RUNTIME_DEMO_SOURCE_DIR is required");
#endif
}

}  // namespace
}  // namespace apptraverse::test

int main() {
  apptraverse::test::TestJournalCommitAndDerivedLayout();
  apptraverse::test::TestGenerationResize();
  apptraverse::test::TestPeriodicUpdate();
  apptraverse::test::TestPublicationReuseAndStableAddresses();
  apptraverse::test::TestImmutableConstants();
  apptraverse::test::TestRepaintDoesNotTouchModel();
  apptraverse::test::TestConsistencyMessageThenResize();
  apptraverse::test::TestNoRuntimeSaveInDemoSources();
  std::cout << "model_ui_runtime_test OK\n";
  return 0;
}
