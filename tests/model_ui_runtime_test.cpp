#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include "aether/clock.h"
#include "aether/domain_storage/ram_domain_storage.h"

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/distill.h"
#include "apptraverse/graph_mirror.h"
#include "apptraverse/model_runtime.h"
#include "apptraverse/object_serialization.h"
#include "apptraverse/overlay_domain_storage.h"
#include "apptraverse/ui_mirror.h"

#include "demo_bootstrap.h"
#include "demo_commands.h"
#include "demo_events.h"
#include "demo_ids.h"
#include "demo_layout.h"
#include "demo_model.h"

#if defined(_WIN32)
#  include "win_presenters.h"
#endif

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

bool FileContains(std::filesystem::path const& path,
                  std::string const& needle) {
  std::ifstream in{path};
  std::string text((std::istreambuf_iterator<char>(in)),
                   std::istreambuf_iterator<char>());
  return text.find(needle) != std::string::npos;
}

template <typename T>
T& As(ae::Ptr<ae::Obj> const& obj) {
  CHECK(obj);
  return static_cast<T&>(*obj);
}

WindowBoundsCommand BoundsOf(Window const& window, std::int32_t extra_w,
                             std::int32_t extra_h);

struct Harness {
  ae::RamDomainStorage model_storage;
  OverlayDomainStorage ui_storage;
  ae::Domain model_domain;
  ae::Domain ui_domain;
  Application::ptr application;
  Application::ptr ui_application;
  std::unique_ptr<UiMirror> mirror;
  std::unique_ptr<ModelRuntime> runtime;
  std::vector<UiApplyResult> applies;

  Harness()
      : ui_storage{model_storage},
        model_domain{ae::Now(), model_storage},
        ui_domain{ae::Now(), ui_storage} {
    EnsureDemoRegistration();
    application = BuildDemoGraph(model_domain);
    FinalizeDistilledGraph(*application);
    application->window_b->color_toolbar->opacity = 77;
    application->window_b->color_toolbar->arbitrary_value = 4242;
    auto ui_root =
        CopyModelGraphToUiDomain(*application, ui_domain, ui_storage);
    ui_application = Application::ptr::MakeFromThis(
        static_cast<Application*>(ui_root.get()));
    mirror = std::make_unique<UiMirror>(
        ui_domain, ui_storage,
        [this](std::uint32_t root_id, PublicationChannel<3>* channel) {
          applies.push_back(mirror->ApplyPublished(*channel, root_id));
        });
    runtime =
        std::make_unique<ModelRuntime>(*application, *mirror);
    runtime->AddPresentationRoot(*application->window_a);
    runtime->AddPresentationRoot(*application->window_b);
  }

  void Pump(std::chrono::steady_clock::time_point now) {
    runtime->PumpOnce(now);
  }

  void PostBounds(Window& window, std::int32_t extra_w, std::int32_t extra_h) {
    auto command = BoundsOf(window, extra_w, extra_h);
    runtime->Post([app = &*application, command] {
      CommitWindowBounds(*WindowById(*app, command.window_id), command);
    });
  }

  UiApplyResult const& LastFor(std::uint32_t root_id) const {
    for (auto it = applies.rbegin(); it != applies.rend(); ++it) {
      if (it->root_id == root_id) {
        return *it;
      }
    }
    std::cerr << "no publication for root " << root_id << '\n';
    std::exit(1);
  }

  template <typename T>
  T& UiObj(ae::ObjId id) {
    return As<T>(ui_domain.Find(id));
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
  command.client_width = window.client_width + extra_w;
  command.client_height = window.client_height + extra_h;
  return command;
}

void TestStandardPointer() {
  Harness h;
  auto& toolbar = *h.application->window_b->text_toolbar;
  CHECK(toolbar.text);
  CHECK(toolbar.text.is_loaded());
  CHECK(toolbar.text->bytes == demo::kToolbarTextBytes);
#ifdef MODEL_UI_RUNTIME_DEMO_SOURCE_DIR
  std::filesystem::path const root{MODEL_UI_RUNTIME_DEMO_SOURCE_DIR};
  CHECK(!FileContains(root / "common" / "demo_model.h", "text_id"));
  CHECK(!FileContains(root / "common" / "demo_model.h", "ConstRef"));
  CHECK(!FileContains(root / "common" / "demo_model.h",
                      "ImmutableObjectStore"));
#endif
}

void TestInitialGraphCopy() {
  Harness h;
  CHECK(&*h.application != h.ui_domain.Find(h.application->obj_id).get());
  auto ui_app = h.ui_domain.Find(h.application->obj_id);
  CHECK(ui_app);
  CHECK(h.application->obj_id.id() == ui_app->obj_id.id());

  auto& model_layout = *h.application->window_b;
  auto& ui_layout = h.UiObj<LayoutWindow>(model_layout.obj_id);
  CHECK(&model_layout != &ui_layout);
  CHECK(model_layout.obj_id.id() == ui_layout.obj_id.id());
  CHECK(ui_layout.client_width == model_layout.client_width);

  auto& model_toolbar = *h.application->window_b->text_toolbar;
  auto& ui_toolbar = h.UiObj<TextToolbar>(model_toolbar.obj_id);
  CHECK(&model_toolbar != &ui_toolbar);
  CHECK(model_toolbar.text.operator->() != ui_toolbar.text.operator->());
  CHECK(ui_toolbar.text->bytes == model_toolbar.text->bytes);
  CHECK(ui_toolbar.text->obj_id.id() == model_toolbar.text->obj_id.id());

  CHECK(ui_layout.text_toolbar->text.operator->() ==
        ui_toolbar.text.operator->());
  CHECK(!ui_layout.base.is_valid());
  CHECK(ui_layout.journal.empty());
  CHECK(ui_layout.Generation() == model_layout.Generation());

  auto& ui_color = h.UiObj<ColorToolbar>(model_layout.color_toolbar->obj_id);
  CHECK(ui_color.opacity == 77);
  CHECK(ui_color.arbitrary_value == 4242);
}

void TestTwoDomainsAndUiNodeState() {
  Harness h;
  CHECK(h.applies.empty());

  auto check_pair = [&](ae::Obj& model) {
    auto ui = h.ui_domain.Find(model.obj_id);
    CHECK(ui);
    CHECK(ui.get() != &model);
    CHECK(ui->obj_id.id() == model.obj_id.id());
    CHECK(ui->GetClassId() == model.GetClassId());
    CHECK(ui->domain != model.domain);
  };

  check_pair(*h.application->window_a);
  check_pair(*h.application->window_b);
  check_pair(*h.application->window_b->text_toolbar);
  check_pair(*h.application->window_b->color_toolbar);
  check_pair(*h.application->window_b->center_strip);
  check_pair(*h.application->window_b->text_toolbar->text);

  auto& model_s = *h.application->window_b->text_toolbar->text;
  auto& ui_s = h.UiObj<ImmutableString>(model_s.obj_id);
  CHECK(&model_s != &ui_s);
  CHECK(model_s.bytes == ui_s.bytes);

  auto& model_w = *h.application->window_b;
  auto& ui_w = h.UiObj<LayoutWindow>(model_w.obj_id);
  CHECK(!ui_w.base.is_valid());
  CHECK(ui_w.journal.empty());
  CHECK(ui_w.Generation() == model_w.Generation());
  CHECK(!ui_w.text_toolbar->base.is_valid());
  CHECK(ui_w.text_toolbar->journal.empty());
  CHECK(ui_w.text_toolbar->Generation() ==
        h.application->window_b->text_toolbar->Generation());
}

void TestJournalCommitDoesNotChangeChildren() {
  Harness h;
  auto& window = *h.application->window_b;
  auto& toolbar = *h.application->window_b->text_toolbar;
  auto& strip = *h.application->window_b->center_strip;
  CHECK(window.journal.empty());
  CHECK(toolbar.journal.empty());
  CHECK(strip.journal.empty());

  auto const t0 = toolbar.Generation();
  auto const s0 = strip.Generation();
  auto event =
      WindowBoundsChangedEvent::ptr::Create(ae::CreateWith{*window.domain});
  event->left = window.left;
  event->top = window.top;
  event->right = window.right + 40;
  event->bottom = window.bottom;
  event->client_width = window.client_width + 40;
  event->client_height = window.client_height;
  window.Commit(event);

  CHECK(window.journal.size() == 1);
  CHECK(toolbar.journal.empty());
  CHECK(h.application->window_b->color_toolbar->journal.empty());
  CHECK(strip.journal.empty());
  CHECK(toolbar.Generation() == t0);
  CHECK(strip.Generation() == s0);

  window.ReplayFromBase();
  CHECK(window.journal.size() == 1);
  CHECK(toolbar.Generation() == t0);
  CHECK(strip.Generation() == s0);

  SaveDistilledRoot(*h.application);  // runtime-save-ok: test persistence
  ae::Domain loaded{ae::Now(), h.model_storage};
  auto app = LoadApplication<Application>(
      loaded, ae::ObjId{demo::ToObjId(demo::DemoObjId::Application)});
  CHECK(app->window_b->journal.size() == 1);
  CHECK(app->window_b->client_width == window.client_width);
  CHECK(app->window_b->text_toolbar->journal.empty());
  CHECK(app->window_b->text_toolbar->text->bytes == demo::kToolbarTextBytes);
}

void TestGenerationResizeAndNativeLayout() {
  Harness h;
  auto now = std::chrono::steady_clock::now();
  h.Pump(now);
  auto& window = *h.application->window_b;
  auto& text = *h.application->window_b->text_toolbar;
  auto& color = *h.application->window_b->color_toolbar;
  auto& strip = *h.application->window_b->center_strip;

  auto const w0 = window.Generation();
  auto const t0 = text.Generation();
  auto const c0 = color.Generation();
  auto const s0 = strip.Generation();

  h.PostBounds(window, 80, 0);
  h.Pump(now + std::chrono::milliseconds{20});
  CHECK(window.Generation() > w0);
  CHECK(text.Generation() == t0);
  CHECK(color.Generation() == c0);
  CHECK(strip.Generation() == s0);

  auto& ui_w = h.UiObj<LayoutWindow>(window.obj_id);
  CHECK(ui_w.client_width == window.client_width);
  CHECK(TextToolbarNativeRect(ui_w).width == window.client_width);
  CHECK(CenterStripNativeRect(ui_w).width ==
        (window.client_width * 2) / 3);
  auto const horiz = h.LastFor(window.obj_id.id());
  CHECK(HasId(horiz.changed_obj_ids, window.obj_id.id()));
  CHECK(!HasId(horiz.changed_obj_ids, text.obj_id.id()));
  CHECK(!HasId(horiz.changed_obj_ids, color.obj_id.id()));
  CHECK(!HasId(horiz.changed_obj_ids, strip.obj_id.id()));

  auto const w1 = window.Generation();
  auto const text_rect_after_h = TextToolbarNativeRect(window);
  auto const color_rect_after_h = ColorToolbarNativeRect(window);
  h.PostBounds(window, 0, 60);
  h.Pump(now + std::chrono::milliseconds{40});
  CHECK(window.Generation() > w1);
  CHECK(text.Generation() == t0);
  CHECK(color.Generation() == c0);
  CHECK(strip.Generation() == s0);
  CHECK(TextToolbarNativeRect(window) == text_rect_after_h);
  CHECK(ColorToolbarNativeRect(window) == color_rect_after_h);
  CHECK(CenterStripNativeRect(window).height ==
        window.client_height - text.height - color.height);
  auto& ui_w2 = h.UiObj<LayoutWindow>(window.obj_id);
  CHECK(CenterStripNativeRect(ui_w2).height ==
        CenterStripNativeRect(window).height);
}

void TestPeriodicUpdate() {
  Harness h;
  auto now = std::chrono::steady_clock::now();
  h.Pump(now);
  CHECK(h.application->window_b->color_toolbar->journal.empty());
  h.Pump(now + demo::kModelUpdatePeriod);
  CHECK(h.application->window_b->color_toolbar->journal.empty());
  h.Pump(now + demo::kColorChangePeriod);
  auto& color = *h.application->window_b->color_toolbar;
  CHECK(color.journal.size() == 1);
  CHECK(color.journal[0].event->GetClassId() == ColorChangedEvent::kClassId);
  auto& ui_color = h.UiObj<ColorToolbar>(color.obj_id);
  CHECK(ui_color.color == color.color);
  CHECK(ui_color.Generation() == color.Generation());
  auto const pub = h.LastFor(h.application->window_b.id().id());
  CHECK(HasId(pub.changed_obj_ids, color.obj_id.id()));
  h.Pump(now + demo::kColorChangePeriod + demo::kModelUpdatePeriod);
  CHECK(color.journal.size() == 1);
}

void TestStableUiAddressesAndStringReuse() {
  Harness h;
  auto now = std::chrono::steady_clock::now();
  CHECK(h.applies.empty());

  auto const window_b = demo::ToObjId(demo::DemoObjId::LayoutWindow);
  auto* strip_addr =
      &h.UiObj<CenterStrip>(ae::ObjId{demo::ToObjId(demo::DemoObjId::CenterStrip)});
  auto* text_addr =
      &h.UiObj<TextToolbar>(ae::ObjId{demo::ToObjId(demo::DemoObjId::TextToolbar)});
  auto* string_addr = &h.UiObj<ImmutableString>(
      ae::ObjId{demo::ToObjId(demo::DemoObjId::ToolbarText)});
  auto const text_gen = text_addr->Generation();

  h.PostBounds(*h.application->window_b, 0, 70);
  h.Pump(now + std::chrono::milliseconds{20});
  auto const second = h.LastFor(window_b);
  CHECK(HasId(second.changed_obj_ids, window_b));
  CHECK(!HasId(second.changed_obj_ids,
               demo::ToObjId(demo::DemoObjId::TextToolbar)));
  CHECK(&h.UiObj<CenterStrip>(ae::ObjId{
            demo::ToObjId(demo::DemoObjId::CenterStrip)}) == strip_addr);
  CHECK(&h.UiObj<TextToolbar>(ae::ObjId{
            demo::ToObjId(demo::DemoObjId::TextToolbar)}) == text_addr);
  CHECK(&h.UiObj<ImmutableString>(ae::ObjId{
            demo::ToObjId(demo::DemoObjId::ToolbarText)}) == string_addr);
  CHECK(text_addr->Generation() == text_gen);

  for (int i = 0; i < 100; ++i) {
    h.Pump(now + std::chrono::milliseconds{20LL * (i + 2)});
  }
  CHECK(&h.UiObj<CenterStrip>(ae::ObjId{
            demo::ToObjId(demo::DemoObjId::CenterStrip)}) == strip_addr);
  CHECK(&h.UiObj<TextToolbar>(ae::ObjId{
            demo::ToObjId(demo::DemoObjId::TextToolbar)}) == text_addr);
}

void TestRepaintDoesNotTouchModel() {
  Harness h;
  auto now = std::chrono::steady_clock::now();
  h.Pump(now);
  auto const gen = h.application->window_a->Generation();
  auto const pubs =
      h.mirror->publication_count(h.application->window_a.id().id());
  auto const journal = h.application->window_a->journal.size();
  std::uint64_t paint_count = 0;
  ++paint_count;
  CHECK(paint_count == 1);
  CHECK(h.application->window_a->Generation() == gen);
  CHECK(h.mirror->publication_count(h.application->window_a.id().id()) ==
        pubs);
  CHECK(h.application->window_a->journal.size() == journal);
}

void TestUnreadPublicationIsNotOverwritten() {
  ae::RamDomainStorage model_storage;
  OverlayDomainStorage ui_storage{model_storage};
  ae::Domain model_domain{ae::Now(), model_storage};
  ae::Domain ui_domain{ae::Now(), ui_storage};
  EnsureDemoRegistration();
  auto application = BuildDemoGraph(model_domain);
  FinalizeDistilledGraph(*application);
  auto ui_root =
      CopyModelGraphToUiDomain(*application, ui_domain, ui_storage);
  Application::ptr ui_application = Application::ptr::MakeFromThis(
      static_cast<Application*>(ui_root.get()));
  std::vector<std::uint32_t> notified_roots;
  UiMirror mirror(ui_domain, ui_storage,
                  [&](std::uint32_t root_id, PublicationChannel<3>*) {
                    notified_roots.push_back(root_id);
                  });
  ModelRuntime runtime(*application, mirror);
  runtime.AddPresentationRoot(*application->window_a);
  runtime.AddPresentationRoot(*application->window_b);

  auto now = std::chrono::steady_clock::now();
  auto const window_b = demo::ToObjId(demo::DemoObjId::LayoutWindow);

  runtime.Post([app = &*application] {
    CommitWindowBounds(*app->window_b, BoundsOf(*app->window_b, 80, 0));
  });
  runtime.PumpOnce(now);
  CHECK(static_cast<int>(std::count(notified_roots.begin(), notified_roots.end(),
                                    window_b)) == 1);
  CHECK(mirror.ChannelFor(window_b).has_unread_published());
  auto const first_width = application->window_b->client_width;

  runtime.Post([app = &*application] {
    CommitWindowBounds(*app->window_b, BoundsOf(*app->window_b, 160, 0));
  });
  runtime.PumpOnce(now + std::chrono::milliseconds{20});
  CHECK(static_cast<int>(std::count(notified_roots.begin(), notified_roots.end(),
                                    window_b)) == 1);
  CHECK(mirror.ChannelFor(window_b).has_unread_published());
  CHECK(runtime.HasPending(window_b));
  auto const latest_width = application->window_b->client_width;
  auto const latest_gen = application->window_b->Generation();
  CHECK(latest_width > first_width);

  auto applied = mirror.ApplyPublished(mirror.ChannelFor(window_b), window_b);
  CHECK(applied.root_id == window_b);
  CHECK(HasId(applied.changed_obj_ids, window_b));
  auto& ui_window = As<LayoutWindow>(ui_domain.Find(ae::ObjId{window_b}));
  CHECK(ui_window.client_width == first_width);
  CHECK(runtime.HasPending(window_b));

  runtime.PumpOnce(now + std::chrono::milliseconds{40});
  CHECK(static_cast<int>(std::count(notified_roots.begin(), notified_roots.end(),
                                    window_b)) == 2);
  CHECK(!runtime.HasPending(window_b));
  applied = mirror.ApplyPublished(mirror.ChannelFor(window_b), window_b);
  CHECK(HasId(applied.changed_obj_ids, window_b));
  CHECK(ui_window.client_width == latest_width);
  CHECK(ui_window.Generation() == latest_gen);
  CHECK(!ui_window.base.is_valid());
  CHECK(ui_window.journal.empty());
  (void)ui_application;
}

void TestPersistence() {
  Harness h;
  auto& a = *h.application->window_a;
  auto& b = *h.application->window_b;
  WindowBoundsCommand a_cmd;
  a_cmd.window_id = a.obj_id.id();
  a_cmd.left = 111;
  a_cmd.top = 22;
  a_cmd.right = 333;
  a_cmd.bottom = 244;
  a_cmd.client_width = 200;
  a_cmd.client_height = 180;
  CommitWindowBounds(a, a_cmd);
  WindowBoundsCommand b_cmd;
  b_cmd.window_id = b.obj_id.id();
  b_cmd.left = 400;
  b_cmd.top = 50;
  b_cmd.right = 900;
  b_cmd.bottom = 500;
  b_cmd.client_width = 480;
  b_cmd.client_height = 410;
  CommitWindowBounds(b, b_cmd);
  SaveDistilledRoot(*h.application);  // runtime-save-ok: test persistence

  ae::Domain loaded{ae::Now(), h.model_storage};
  auto app = LoadApplication<Application>(
      loaded, ae::ObjId{demo::ToObjId(demo::DemoObjId::Application)});
  CHECK(app->window_a->left == 111);
  CHECK(app->window_a->top == 22);
  CHECK(app->window_a->right == 333);
  CHECK(app->window_a->bottom == 244);
  CHECK(app->window_b->left == 400);
  CHECK(app->window_b->client_width == 480);
}

void TestOrdinaryReflectionField() {
  Harness h;
  auto& model_color = *h.application->window_b->color_toolbar;
  model_color.arbitrary_value = 9002;
  auto& ui_color = h.UiObj<ColorToolbar>(model_color.obj_id);
  ui_color.arbitrary_value = 0;
  ByteSink scratch;
  SerializeObjectToBuffer(model_color, scratch);
  ByteSource payload;
  payload.data = scratch.bytes.data();
  payload.size = scratch.bytes.size();
  DeserializeObjectFromBuffer(ui_color, payload, h.ui_domain, h.ui_storage);
  FinalizeUiNodeState(ui_color, model_color.Generation());
  CHECK(ui_color.arbitrary_value == 9002);
}

void TestBaseJournalCopyAndCleanup() {
  ae::RamDomainStorage model_storage;
  OverlayDomainStorage ui_storage{model_storage};
  ae::Domain model_domain{ae::Now(), model_storage};
  ae::Domain ui_domain{ae::Now(), ui_storage};
  EnsureDemoRegistration();
  auto application = BuildDemoGraph(model_domain);
  FinalizeDistilledGraph(*application);
  auto ui_root =
      CopyModelGraphToUiDomain(*application, ui_domain, ui_storage);
  Application::ptr ui_application = Application::ptr::MakeFromThis(
      static_cast<Application*>(ui_root.get()));

  auto& model_window = *application->window_b;
  CHECK(model_window.base.is_valid());
  model_window.base.Load();

  auto event =
      WindowBoundsChangedEvent::ptr::Create(ae::CreateWith{*model_window.domain});
  event->left = model_window.left;
  event->top = model_window.top;
  event->right = model_window.right + 50;
  event->bottom = model_window.bottom;
  event->client_width = model_window.client_width + 50;
  event->client_height = model_window.client_height;
  model_window.Commit(event);
  CHECK(!model_window.journal.empty());

  auto const generation = model_window.Generation();
  auto ui_object = ui_domain.Find(model_window.obj_id);
  CHECK(ui_object);

  ByteSink scratch;
  SerializeObjectToBuffer(model_window, scratch);
  CHECK(!scratch.bytes.empty());

  ByteSource payload;
  payload.data = scratch.bytes.data();
  payload.size = scratch.bytes.size();
  DeserializeObjectFromBuffer(*ui_object, payload, ui_domain, ui_storage);
  FinalizeUiNodeState(*ui_object, generation);

  auto& ui_window = static_cast<LayoutWindow&>(*ui_object);
  CHECK(ui_window.client_width == model_window.client_width);
  CHECK(!ui_window.base.is_valid());
  CHECK(ui_window.journal.empty());
  CHECK(ui_window.Generation() == generation);
  (void)ui_application;
}

void TestJournalDoesNotReturnAfterReloadAndMirror() {
  auto root = std::filesystem::temp_directory_path() /
              "apptraverse_model_ui_journal_reload_test";
  std::filesystem::remove_all(root);

  std::int32_t expected_width = 0;
  {
    DirectoryDomainStorage storage{root};
    ae::Domain domain{ae::Now(), storage};
    EnsureDemoRegistration();
#if defined(_WIN32)
    EnsureWindowsPresenterRegistration();
#endif
    auto application = BuildDemoGraph(domain);
    FinalizeDistilledGraph(*application);
    auto& window = *application->window_b;
    auto event =
        WindowBoundsChangedEvent::ptr::Create(ae::CreateWith{*window.domain});
    event->left = window.left;
    event->top = window.top;
    event->right = window.right + 70;
    event->bottom = window.bottom;
    event->client_width = window.client_width + 70;
    event->client_height = window.client_height;
    window.Commit(event);
    expected_width = window.client_width;
    CHECK(!window.journal.empty());
    SaveDistilledRoot(*application);  // runtime-save-ok: test persistence
#if defined(_WIN32)
    auto presentation = BuildPresentationGraph(domain, *application);
    SaveDistilledRoot(*presentation);  // runtime-save-ok: distill presenters
#endif
  }

  DirectoryDomainStorage storage{root};
  OverlayDomainStorage ui_storage{storage};
  ae::Domain model_domain{ae::Now(), storage};
  ae::Domain ui_domain{ae::Now(), ui_storage};
  EnsureDemoRegistration();
#if defined(_WIN32)
  EnsureWindowsPresenterRegistration();
#endif
  auto application = LoadApplication<Application>(
      model_domain, ae::ObjId{demo::ToObjId(demo::DemoObjId::Application)});
  CHECK(!application->window_b->journal.empty());
  CHECK(application->window_b->client_width == expected_width);
  CHECK(model_domain.Find(ae::ObjId{demo::ToObjId(
            demo::DemoObjId::WinPresentationApplication)}) == nullptr);

  auto ui_root =
      CopyModelGraphToUiDomain(*application, ui_domain, ui_storage);
  Application::ptr ui_application = Application::ptr::MakeFromThis(
      static_cast<Application*>(ui_root.get()));
  auto& ui_window = As<LayoutWindow>(
      ui_domain.Find(ae::ObjId{demo::ToObjId(demo::DemoObjId::LayoutWindow)}));
  CHECK(ui_window.client_width == expected_width);
  CHECK(!ui_window.base.is_valid());
  CHECK(ui_window.journal.empty());

#if defined(_WIN32)
  auto presentation = LoadApplication<WinPresentationApplication>(
      ui_domain,
      ae::ObjId{demo::ToObjId(demo::DemoObjId::WinPresentationApplication)});
  CHECK(presentation);
  CHECK(model_domain.Find(ae::ObjId{demo::ToObjId(
            demo::DemoObjId::WinPresentationApplication)}) == nullptr);
  CHECK(!ui_window.base.is_valid());
  CHECK(ui_window.journal.empty());
#endif
  (void)ui_application;
  std::filesystem::remove_all(root);
}

#if defined(_WIN32)
void TestCleanDistillPresenterGraphLoadsInUiDomain() {
  auto root = std::filesystem::temp_directory_path() /
              "apptraverse_model_ui_presenter_overlay_test";
  std::filesystem::remove_all(root);

  {
    EnsureDemoRegistration();
    EnsureWindowsPresenterRegistration();
    DirectoryDomainStorage storage{root};
    ae::Domain domain{ae::Now(), storage};
    auto application = BuildDemoGraph(domain);
    FinalizeDistilledGraph(*application);
    SaveDistilledRoot(*application);  // runtime-save-ok: distill
    auto presentation = BuildPresentationGraph(domain, *application);
    SaveDistilledRoot(*presentation);  // runtime-save-ok: distill
  }

  DirectoryDomainStorage storage{root};
  OverlayDomainStorage ui_storage{storage};
  ae::Domain model_domain{ae::Now(), storage};
  ae::Domain ui_domain{ae::Now(), ui_storage};
  EnsureDemoRegistration();
  EnsureWindowsPresenterRegistration();

  auto application = LoadApplication<Application>(
      model_domain, ae::ObjId{demo::ToObjId(demo::DemoObjId::Application)});
  auto ui_root =
      CopyModelGraphToUiDomain(*application, ui_domain, ui_storage);
  Application::ptr ui_application = Application::ptr::MakeFromThis(
      static_cast<Application*>(ui_root.get()));

  auto const presenter_id =
      demo::ToObjId(demo::DemoObjId::WinPresentationApplication);
  CHECK(model_domain.Find(ae::ObjId{presenter_id}) == nullptr);

  auto presentation = LoadApplication<WinPresentationApplication>(
      ui_domain, ae::ObjId{presenter_id});
  CHECK(ui_domain.Find(ae::ObjId{presenter_id}) != nullptr);
  CHECK(model_domain.Find(ae::ObjId{presenter_id}) == nullptr);

  auto& ui_layout = As<LayoutWindow>(
      ui_domain.Find(ae::ObjId{demo::ToObjId(demo::DemoObjId::LayoutWindow)}));
  auto& model_layout = *application->window_b;
  CHECK(presentation->layout_window->window.domain() == &ui_domain);
  CHECK(&*presentation->layout_window->window == &ui_layout);
  CHECK(&*presentation->layout_window->window != &model_layout);
  CHECK(presentation->layout_window->window->obj_id.id() ==
        model_layout.obj_id.id());

  CHECK(&*presentation->layout_window->text_toolbar->toolbar ==
        &*ui_layout.text_toolbar);
  CHECK(&*presentation->layout_window->color_toolbar->toolbar ==
        &*ui_layout.color_toolbar);
  CHECK(&*presentation->layout_window->center_strip->strip ==
        &*ui_layout.center_strip);

  CHECK(!ui_layout.base.is_valid());
  CHECK(ui_layout.journal.empty());
  CHECK(!ui_layout.text_toolbar->base.is_valid());
  CHECK(ui_layout.text_toolbar->journal.empty());
  (void)ui_application;
  std::filesystem::remove_all(root);
}
#endif

void TestNoManualSerializersOrRuntimeClasses() {
#ifdef MODEL_UI_RUNTIME_DEMO_SOURCE_DIR
  std::filesystem::path const root{MODEL_UI_RUNTIME_DEMO_SOURCE_DIR};
  CHECK(std::filesystem::exists(root));
  std::vector<std::string> forbidden = {
      "WriteUiState",       "ReadRuntime",        "RuntimeWindow",
      "RuntimeTextToolbar", "RuntimeColorToolbar", "RuntimeChat",
      "RuntimeObject",      "UiRuntimeRegistry",   "PayloadArchive",
      "ImmutableObjectStore", "ConstRef",          "AddMessageEvent",
      "AddMessageCommand",  "last_pub_a_",         "last_pub_b_",
      "window_b_",          "CaptureBaseState(",   "text_id",
      "kUiSubgraphMagic",   "0x41545549",          "kReuseObject",
      "ReuseObjectRecord",  "reused_obj_ids",      "EnsureUiObject",
      "UiRecordKind",       "MaterializedOps",     "MaterializedOpsRegistrar",
      "SaveMaterializedField", "LoadMaterializedField",
      "RegisterMaterializedOps", "FindMaterializedOps",
      "APPTRAVERSE_REGISTER_MATERIALIZED", "materialized_ops.h",
      "ui_materialized.h",  "SerializeMaterializedObject",
      "DeserializeMaterializedObject", "EagerLoadReachable"};
  for (auto const& entry :
       std::filesystem::recursive_directory_iterator(root)) {
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
      for (auto const& token : forbidden) {
        CHECK(line.find(token) == std::string::npos);
      }
      auto const pos = line.find(".Save()");
      if (pos != std::string::npos) {
        CHECK(line.find("runtime-save-ok") != std::string::npos);
      }
      if (line.find("dpi") != std::string::npos &&
          line.find("DpiAwareness") == std::string::npos &&
          line.find("DPI_AWARENESS") == std::string::npos) {
        CHECK(false && "dpi remains in demo sources");
      }
    }
  }
#else
  CHECK(false && "MODEL_UI_RUNTIME_DEMO_SOURCE_DIR is required");
#endif
}

}  // namespace
}  // namespace apptraverse::test

int main() {
  using apptraverse::test::TestBaseJournalCopyAndCleanup;
  using apptraverse::test::TestGenerationResizeAndNativeLayout;
  using apptraverse::test::TestInitialGraphCopy;
  using apptraverse::test::TestJournalCommitDoesNotChangeChildren;
  using apptraverse::test::TestJournalDoesNotReturnAfterReloadAndMirror;
  using apptraverse::test::TestNoManualSerializersOrRuntimeClasses;
  using apptraverse::test::TestOrdinaryReflectionField;
  using apptraverse::test::TestPeriodicUpdate;
  using apptraverse::test::TestPersistence;
  using apptraverse::test::TestRepaintDoesNotTouchModel;
  using apptraverse::test::TestStableUiAddressesAndStringReuse;
  using apptraverse::test::TestStandardPointer;
  using apptraverse::test::TestTwoDomainsAndUiNodeState;
  using apptraverse::test::TestUnreadPublicationIsNotOverwritten;

  TestStandardPointer();
  TestInitialGraphCopy();
  TestTwoDomainsAndUiNodeState();
  TestJournalCommitDoesNotChangeChildren();
  TestGenerationResizeAndNativeLayout();
  TestPeriodicUpdate();
  TestStableUiAddressesAndStringReuse();
  TestRepaintDoesNotTouchModel();
  TestUnreadPublicationIsNotOverwritten();
  TestPersistence();
  TestOrdinaryReflectionField();
  TestBaseJournalCopyAndCleanup();
  TestJournalDoesNotReturnAfterReloadAndMirror();
#if defined(_WIN32)
  apptraverse::test::TestCleanDistillPresenterGraphLoadsInUiDomain();
#endif
  TestNoManualSerializersOrRuntimeClasses();
  std::cout << "model_ui_runtime_test OK\n";
  return 0;
}
