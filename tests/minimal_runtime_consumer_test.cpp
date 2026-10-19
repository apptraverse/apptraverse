#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <thread>

#include "apptraverse/application/model_publication_session.h"
#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/distill.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/ui/ui_locale_settings.h"
#include "apptraverse/ui/ui_localization_string.h"
#include "apptraverse/runtime_node.h"
#include "apptraverse/ui/ui_string.h"

namespace apptraverse::test {
namespace {

constexpr ae::ObjId kMinimalConsumerRootId{0xA1B2C001u};

class MinimalConsumerRoot : public ae::Obj {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::MinimalConsumerRoot",
                           MinimalConsumerRoot, ae::Obj, 0)

 protected:
  MinimalConsumerRoot() = default;

 public:
  explicit MinimalConsumerRoot(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(counter), AE_MMBR(locale), AE_MMBR(greeting))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, counter, locale, greeting);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, counter, locale, greeting);
  }

  std::int32_t counter{0};
  ae::ObjPtr<ui::UiLocaleSettings> locale;
  ae::ObjPtr<ui::UiLocalizationString> greeting;
};

APPTRAVERSE_REGISTER(MinimalConsumerRoot);

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << '\n';                           \
      std::exit(1);                                                          \
    }                                                                        \
  } while (0)

MinimalConsumerRoot::ptr BuildMinimalGraph(ae::Domain& domain) {
  auto root = MinimalConsumerRoot::ptr::Create(
      ae::CreateWith{domain}.with_id(kMinimalConsumerRootId));
  root->counter = 7;
  auto locale = ui::UiLocaleSettings::ptr::Create(ae::CreateWith{domain});
  InitializeRuntimeNode(*locale);
  root->locale = locale;
  auto greeting = ui::UiLocalizationString::ptr::Create(ae::CreateWith{domain});
  InitializeRuntimeNode(*greeting);
  auto en = ui::UiString::ptr::Create(ae::CreateWith{domain});
  en->utf8 = "Hello";
  auto ru = ui::UiString::ptr::Create(ae::CreateWith{domain});
  ru->utf8 = "Привет";
  greeting->strings = {en, ru};
  root->greeting = greeting;
  return root;
}

void ConfigureMinimalSession(ModelPublicationSession& session) {
  session.config.root_object_id = kMinimalConsumerRootId;
  session.config.ensure_type_registration = [] { EnsureObjectRegistration(); };
  session.config.load_mutable_root = [&session](ae::Domain& domain) -> ae::Obj* {
    auto root = LoadApplication<MinimalConsumerRoot>(domain, kMinimalConsumerRootId);
    session.model_root = root;
    return &*root;
  };
  session.config.bootstrap_if_missing =
      [](ae::Domain& domain, ae::IDomainStorage& storage) {
        (void)storage;
        auto bootstrap = BuildMinimalGraph(domain);
        FinalizeDistilledGraph(*bootstrap);
        SaveDistilledRoot(*bootstrap);
      };
  session.config.persist_root = [](ae::Obj& root) {
    MinimalConsumerRoot::ptr::MakeFromThis(
        static_cast<MinimalConsumerRoot*>(&root))
        .Save();
  };
}

std::filesystem::path TestDir() {
  auto path = std::filesystem::temp_directory_path() /
              "apptraverse_minimal_runtime_consumer";
  std::filesystem::remove_all(path);
  return path;
}

}  // namespace

void RunMinimalRuntimeConsumerTest() {
  auto const dir = TestDir();
  ModelPublicationSession session;
  session.state_dir = dir;
  ConfigureMinimalSession(session);

  std::thread model{[&] {
    session.Run([&](ModelPublicationKind) { session.cv.notify_all(); });
  }};
  {
    std::unique_lock<std::mutex> lock{session.mu};
    CHECK(session.cv.wait_for(lock, std::chrono::seconds{30}, [&] {
      return session.channel.has_unread_published();
    }));
  }
  session.RequestStop();
  model.join();

  DirectoryDomainStorage storage{dir};
  ae::Domain domain{storage};
  auto reloaded = LoadApplication<MinimalConsumerRoot>(domain, kMinimalConsumerRootId);
  CHECK(reloaded->counter == 7);
  CHECK(reloaded->locale->language_index() == 0);
  CHECK(reloaded->greeting->Resolve(0) == "Hello");
  CHECK(reloaded->greeting->Resolve(1) == "Привет");

  reloaded->counter = 42;
  SaveDistilledRoot(*reloaded);

  auto again = LoadApplication<MinimalConsumerRoot>(domain, kMinimalConsumerRootId);
  CHECK(again->counter == 42);

  std::filesystem::remove_all(dir);
}

}  // namespace apptraverse::test

int main() {
  apptraverse::test::RunMinimalRuntimeConsumerTest();
  std::cout << "minimal_runtime_consumer_test OK\n";
  return 0;
}
