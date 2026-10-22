#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "aether-objects/obj/domain.h"
#include "aether-objects/obj/obj.h"

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/event_for.h"
#include "apptraverse/node_for.h"
#include "apptraverse/object_macros.h"

#ifndef APPTRAVERSE_EVENT_VERSION_EXE_REVISION
#error APPTRAVERSE_EVENT_VERSION_EXE_REVISION is required
#endif
static_assert(APPTRAVERSE_EVENT_VERSION_EXE_REVISION == 0 ||
              APPTRAVERSE_EVENT_VERSION_EXE_REVISION == 1);

namespace apptraverse::test::event_version {

#if APPTRAVERSE_EVENT_VERSION_EXE_REVISION == 0
constexpr unsigned kDocSchema = 0;
constexpr unsigned kEventSchema = 0;
#else
constexpr unsigned kDocSchema = 1;
constexpr unsigned kEventSchema = 1;
#endif

constexpr ae::ObjId::Type kDocId = 94001;
constexpr ae::ObjId::Type kBaseId = 94002;

class VersionProbeDocument;
class SchemaBumpEvent;

class VersionProbeDocument : public NodeFor<VersionProbeDocument> {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::test::event_version::VersionProbeDocument",
      VersionProbeDocument, Node, kDocSchema)

 protected:
  VersionProbeDocument() = default;

 public:
  explicit VersionProbeDocument(ae::ObjProp prop) : NodeFor{prop} {}

#if APPTRAVERSE_EVENT_VERSION_EXE_REVISION >= 1
  AE_OBJECT_REFLECT(AE_MMBR(tally), AE_MMBR(extension))
#else
  AE_OBJECT_REFLECT(AE_MMBR(tally))
#endif

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, tally);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, tally);
  }

#if APPTRAVERSE_EVENT_VERSION_EXE_REVISION >= 1
  template <typename Dnv>
  void Load(ae::Version<1>, Dnv& dnv) {
    dnv(base_, extension);
  }

  template <typename Dnv>
  void Save(ae::Version<1>, Dnv& dnv) const {
    dnv(base_, extension);
  }
#endif

  std::uint32_t tally{0};
#if APPTRAVERSE_EVENT_VERSION_EXE_REVISION >= 1
  std::uint32_t extension{0};
#endif

  void Apply(SchemaBumpEvent const& event);
};

class SchemaBumpEvent : public EventFor<VersionProbeDocument, SchemaBumpEvent> {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::test::event_version::SchemaBumpEvent", SchemaBumpEvent,
      Event, kEventSchema)

 protected:
  SchemaBumpEvent() = default;

 public:
  explicit SchemaBumpEvent(ae::ObjProp prop) : EventFor{prop} {}

#if APPTRAVERSE_EVENT_VERSION_EXE_REVISION >= 1
  AE_OBJECT_REFLECT(AE_MMBR(delta), AE_MMBR(extension_delta),
                    AE_MMBR(extension_delta_set))
#else
  AE_OBJECT_REFLECT(AE_MMBR(delta))
#endif

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, delta);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, delta);
  }

#if APPTRAVERSE_EVENT_VERSION_EXE_REVISION >= 1
  template <typename Dnv>
  void Load(ae::Version<1>, Dnv& dnv) {
    dnv(base_, extension_delta, extension_delta_set);
  }

  template <typename Dnv>
  void Save(ae::Version<1>, Dnv& dnv) const {
    dnv(base_, extension_delta, extension_delta_set);
  }
#endif

  std::int32_t delta{0};
#if APPTRAVERSE_EVENT_VERSION_EXE_REVISION >= 1
  std::int32_t extension_delta{0};
  bool extension_delta_set{false};
#endif
};

APPTRAVERSE_REGISTER(VersionProbeDocument);
APPTRAVERSE_REGISTER(SchemaBumpEvent);

void VersionProbeDocument::Apply(SchemaBumpEvent const& event) {
  tally = static_cast<std::uint32_t>(
      static_cast<std::int32_t>(tally) + event.delta);
#if APPTRAVERSE_EVENT_VERSION_EXE_REVISION >= 1
  if (event.extension_delta_set) {
    extension = static_cast<std::uint32_t>(
        static_cast<std::int32_t>(extension) + event.extension_delta);
  }
#endif
}

void EnsureRegistration() { EnsureObjectRegistration(); }

void RequireEmptyStateDir(std::filesystem::path const& state_dir) {
  if (std::filesystem::exists(state_dir) &&
      (!std::filesystem::is_directory(state_dir) ||
       !std::filesystem::is_empty(state_dir))) {
    throw std::runtime_error("operation requires an empty state directory");
  }
}

void SaveDocument(DirectoryDomainStorage& storage,
                  VersionProbeDocument::ptr const& doc) {
  storage.BeginSaveSession();
  doc.Save();
  if (storage.SaveSessionHadFailure()) {
    throw std::runtime_error("document Save failed");
  }
}

VersionProbeDocument::ptr LoadDocument(ae::Domain& domain) {
  auto doc = VersionProbeDocument::ptr::Declare(
      ae::CreateWith{domain}.with_id(kDocId));
  doc.Load();
  if (!doc.is_loaded()) {
    throw std::runtime_error("document Load failed");
  }
  return doc;
}

std::int32_t ParseDelta(std::string const& text) {
  if (text.empty()) {
    throw std::runtime_error("missing --delta");
  }
  return static_cast<std::int32_t>(std::stol(text));
}

void WriteInspect(VersionProbeDocument::ptr const& doc,
                  DirectoryDomainStorage& storage) {
  std::cout << "{\"ok\":true,\"exe\":\""
            << (APPTRAVERSE_EVENT_VERSION_EXE_REVISION == 0 ? 'A' : 'B')
            << "\",\"document_id\":" << kDocId
            << ",\"document_class_id\":" << VersionProbeDocument::kClassId
            << ",\"event_class_id\":" << SchemaBumpEvent::kClassId
            << ",\"tally\":" << doc->tally
            << ",\"journal_len\":" << doc->journal.size();
#if APPTRAVERSE_EVENT_VERSION_EXE_REVISION >= 1
  std::cout << ",\"extension\":" << doc->extension;
#endif
  std::cout << ",\"events\":[";
  for (std::size_t i = 0; i < doc->journal.size(); ++i) {
    if (i != 0) {
      std::cout << ',';
    }
    auto const& record = doc->journal[i];
    auto event = SchemaBumpEvent::ptr{record.event};
    if (!event.is_loaded()) {
      event.Load();
    }
    auto const layers = storage.Enumerate(record.event.id());
    std::cout << "{\"event_id\":" << record.event.id().id()
              << ",\"event_class_id\":" << SchemaBumpEvent::kClassId
              << ",\"delta\":" << event->delta;
#if APPTRAVERSE_EVENT_VERSION_EXE_REVISION >= 1
    std::cout << ",\"extension_delta\":" << event->extension_delta
              << ",\"extension_delta_set\":"
              << (event->extension_delta_set ? "true" : "false");
#endif
    std::cout << ",\"class_ids\":[";
    for (std::size_t j = 0; j < layers.size(); ++j) {
      if (j != 0) {
        std::cout << ',';
      }
      std::cout << layers[j];
    }
    std::cout << "]}";
  }
  std::cout << "],\"document_class_ids\":[";
  auto const doc_layers = storage.Enumerate(ae::ObjId{kDocId});
  for (std::size_t i = 0; i < doc_layers.size(); ++i) {
    if (i != 0) {
      std::cout << ',';
    }
    std::cout << doc_layers[i];
  }
  std::cout << "]}\n";
}

int Run(int argc, char** argv) {
  std::filesystem::path state_dir;
  std::string operation;
  std::int32_t delta = 0;
  std::int32_t extension_delta = 0;
  bool extension_delta_set = false;

  for (int i = 1; i < argc; ++i) {
    std::string_view const argument{argv[i]};
    if (argument == "--state-dir" && i + 1 < argc) {
      state_dir = argv[++i];
    } else if (argument == "--op" && i + 1 < argc) {
      operation = argv[++i];
    } else if (argument == "--delta" && i + 1 < argc) {
      delta = ParseDelta(argv[++i]);
    } else if (argument == "--extension-delta" && i + 1 < argc) {
      extension_delta = ParseDelta(argv[++i]);
      extension_delta_set = true;
    } else {
      throw std::runtime_error("invalid or incomplete argument");
    }
  }

  if (state_dir.empty()) {
    throw std::runtime_error("missing --state-dir");
  }

  EnsureRegistration();
  DirectoryDomainStorage storage{state_dir};
  ae::Domain domain{storage};

  if (operation == "create") {
    RequireEmptyStateDir(state_dir);
    auto base = VersionProbeDocument::ptr::Create(
        ae::CreateWith{domain}.with_id(kBaseId));
    base->tally = 0;
#if APPTRAVERSE_EVENT_VERSION_EXE_REVISION >= 1
    base->extension = 0;
#endif
    auto doc = VersionProbeDocument::ptr::Create(
        ae::CreateWith{domain}.with_id(kDocId));
    doc->base = base;
    doc->tally = 0;
#if APPTRAVERSE_EVENT_VERSION_EXE_REVISION >= 1
    doc->extension = 0;
#endif
    doc->CaptureBaseState();
    SaveDocument(storage, doc);
    WriteInspect(doc, storage);
    return 0;
  }

#if APPTRAVERSE_EVENT_VERSION_EXE_REVISION >= 1
  if (operation == "export-doc-v1-package") {
    RequireEmptyStateDir(state_dir);
    auto base = VersionProbeDocument::ptr::Create(
        ae::CreateWith{domain}.with_id(kBaseId));
    auto doc = VersionProbeDocument::ptr::Create(
        ae::CreateWith{domain}.with_id(kDocId));
    doc->base = base;
    doc->tally = 0;
    doc->extension = 640;
    storage.BeginSaveSession();
    ae::DomainGraph graph{&domain};
    if (!graph.SaveVersion(ae::Version<1>{}, *doc, ae::ObjId{kDocId}).IsOk() ||
        storage.SaveSessionHadFailure()) {
      throw std::runtime_error("export-doc-v1-package SaveVersion failed");
    }
    WriteInspect(doc, storage);
    return 0;
  }

  if (operation == "install-doc-v1-from-user") {
    if (!std::filesystem::is_directory(state_dir)) {
      throw std::runtime_error("state directory does not exist");
    }
    auto doc = LoadDocument(domain);
    if (doc->extension != 640) {
      throw std::runtime_error("document v1 extension not loaded (missing layer?)");
    }
    WriteInspect(doc, storage);
    return 0;
  }
#endif

  if (operation == "inspect" || operation == "reload") {
    auto doc = LoadDocument(domain);
    WriteInspect(doc, storage);
    return 0;
  }

  if (operation == "commit-v0") {
    auto doc = LoadDocument(domain);
    auto event = SchemaBumpEvent::ptr::Create(ae::CreateWith{domain});
    event->delta = delta;
    doc->Commit(event);
    SaveDocument(storage, doc);
    WriteInspect(doc, storage);
    return 0;
  }

#if APPTRAVERSE_EVENT_VERSION_EXE_REVISION >= 1
  if (operation == "commit-v1") {
    auto doc = LoadDocument(domain);
    auto event = SchemaBumpEvent::ptr::Create(ae::CreateWith{domain});
    event->delta = delta;
    event->extension_delta = extension_delta;
    event->extension_delta_set = true;
    doc->Commit(event);
    SaveDocument(storage, doc);
    WriteInspect(doc, storage);
    return 0;
  }
#endif

  throw std::runtime_error(
      "usage: --state-dir PATH --op "
      "create|inspect|reload|commit-v0|export-doc-v1-package|"
      "install-doc-v1-from-user|commit-v1 "
      "[--delta N] [--extension-delta N]");
}

}  // namespace apptraverse::test::event_version

int main(int argc, char** argv) {
  try {
    return apptraverse::test::event_version::Run(argc, argv);
  } catch (std::exception const& ex) {
    std::cerr << ex.what() << "\n";
    return 1;
  }
}
