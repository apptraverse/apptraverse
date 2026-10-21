#include "surfaces_session_automation.h"

#include <charconv>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>

#include "aether-objects/obj/domain.h"

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/graph_copy_policy.h"
#include "apptraverse/object_serialization.h"
#include "apptraverse/event_record.h"
#include "apptraverse/node.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/surfaces/surfaces_demo_revision.h"
#include "apptraverse/surfaces/surfaces_ids.h"
#include "apptraverse/surfaces/surfaces_model.h"
#include "apptraverse/surfaces/surfaces_registration.h"

namespace apptraverse {

namespace {

using surfaces_demo::ObjId;
using surfaces_demo::ToObjId;

constexpr ae::ObjId kSurface1Id{ToObjId(ObjId::Surface1)};

std::vector<std::string> Split(std::string const& input, char delim) {
  std::vector<std::string> parts;
  std::string part;
  std::istringstream stream{input};
  while (std::getline(stream, part, delim)) {
    if (!part.empty()) {
      parts.push_back(part);
    }
  }
  return parts;
}

std::int32_t ParseI32(std::string const& text) {
  std::int32_t value = 0;
  auto const result = std::from_chars(text.data(), text.data() + text.size(),
                                      value);
  if (result.ec != std::errc{}) {
    throw std::runtime_error("invalid integer: " + text);
  }
  return value;
}

Surface* FindSurface1(Application& application) {
  for (auto const& surface : application.surfaces->surfaces) {
    if (surface->obj_id == kSurface1Id) {
      return &*surface;
    }
  }
  return nullptr;
}

#if APPTRAVERSE_SURFACE_DEMO_REVISION == 1
std::size_t CountSuffixEvents(Surface const& surface) {
  Node const& node = static_cast<Node const&>(surface);
  std::size_t count = 0;
  for (EventRecord const& record : node.journal) {
    if (record.event &&
        record.event->GetClassId() ==
            SurfaceWindowTitleSuffixChangedEvent::kClassId) {
      ++count;
    }
  }
  return count;
}
#endif

void PrintInspectJson(Application const& application, bool load_ok,
                      char const* load_error) {
  std::cout << "{\"load_ok\":" << (load_ok ? "true" : "false")
            << ",\"exe\":\""
            << (APPTRAVERSE_SURFACE_DEMO_REVISION == 0 ? 'A' : 'B')
            << "\",\"class_id\":" << Surface::kClassId;
  if (!load_ok) {
    std::cout << ",\"load_error\":" << std::quoted(load_error ? load_error : "")
              << ",\"surfaces\":[]}\n";
    return;
  }
#if APPTRAVERSE_SURFACE_DEMO_REVISION == 1
  std::cout << ",\"suffix_event_class_id\":"
            << SurfaceWindowTitleSuffixChangedEvent::kClassId;
#endif
  std::cout << ",\"surfaces\":[";
  bool first = true;
  for (auto const& surface : application.surfaces->surfaces) {
    if (!first) {
      std::cout << ',';
    }
    first = false;
    std::cout << "{\"object_id\":" << surface->obj_id.id()
              << ",\"desktop_x\":" << surface->desktop_x
              << ",\"desktop_y\":" << surface->desktop_y
              << ",\"desktop_width\":" << surface->desktop_width
              << ",\"desktop_height\":" << surface->desktop_height;
#if APPTRAVERSE_SURFACE_DEMO_REVISION == 1
    std::cout << ",\"window_title_suffix\":"
              << std::quoted(surface->window_title_suffix)
              << ",\"suffix_event_count\":" << CountSuffixEvents(*surface);
#endif
    std::cout << '}';
  }
  std::cout << "]}\n";
}

}  // namespace

std::vector<SurfacesSessionAutomationStep> ParseSurfacesSessionAutomation(
    std::string const& spec) {
  std::vector<SurfacesSessionAutomationStep> steps;
  for (std::string const& token : Split(spec, ';')) {
    if (token.rfind("verify-bounds=", 0) == 0) {
      auto const parts = Split(token.substr(14), ',');
      if (parts.size() != 4) {
        throw std::runtime_error("verify-bounds expects four integers");
      }
      SurfacesSessionAutomationStep step;
      step.op = SurfacesSessionAutomationOp::VerifyBounds;
      step.x = ParseI32(parts[0]);
      step.y = ParseI32(parts[1]);
      step.width = ParseI32(parts[2]);
      step.height = ParseI32(parts[3]);
      steps.push_back(std::move(step));
      continue;
    }
    if (token.rfind("set-bounds=", 0) == 0) {
      auto const parts = Split(token.substr(11), ',');
      if (parts.size() != 4) {
        throw std::runtime_error("set-bounds expects four integers");
      }
      SurfacesSessionAutomationStep step;
      step.op = SurfacesSessionAutomationOp::SetBounds;
      step.x = ParseI32(parts[0]);
      step.y = ParseI32(parts[1]);
      step.width = ParseI32(parts[2]);
      step.height = ParseI32(parts[3]);
      steps.push_back(std::move(step));
      continue;
    }
    if (token.rfind("set-suffix=", 0) == 0) {
#if APPTRAVERSE_SURFACE_DEMO_REVISION != 1
      throw std::runtime_error("set-suffix requires EXE B");
#else
      SurfacesSessionAutomationStep step;
      step.op = SurfacesSessionAutomationOp::SetSuffix;
      step.text = token.substr(11);
      steps.push_back(std::move(step));
      continue;
#endif
    }
    if (token.rfind("verify-title-suffix=", 0) == 0) {
      SurfacesSessionAutomationStep step;
      step.op = SurfacesSessionAutomationOp::VerifyTitleSuffix;
      step.text = token.substr(20);
      steps.push_back(std::move(step));
      continue;
    }
    if (token == "shutdown") {
      steps.push_back({SurfacesSessionAutomationOp::Shutdown});
      continue;
    }
    throw std::runtime_error("unknown session automation token: " + token);
  }
  if (steps.empty()) {
    throw std::runtime_error("session automation spec is empty");
  }
  return steps;
}

int RunSurfacesSessionInspect(std::filesystem::path const& state_dir) {
  EnsureObjectRegistration();
  EnsureSurfacesRegistration();
  DirectoryDomainStorage storage{state_dir};
  ae::Domain domain{storage};
  ae::DomainGraph graph{&domain};
  ae::Ptr<ae::Obj> loaded;
  ae::ObjId const app_id{ToObjId(ObjId::Application)};
  ae::seri::SeriResult const load_result = graph.LoadRoot(app_id, loaded);
  if (!load_result.IsOk() || !loaded) {
    std::cout << "{\"load_ok\":false,\"exe\":\""
              << (APPTRAVERSE_SURFACE_DEMO_REVISION == 0 ? 'A' : 'B')
              << "\",\"load_error\":\"LoadRoot failed\",\"surfaces\":[]}\n";
    return 2;
  }
  if (loaded->GetClassId() != Application::kClassId) {
    std::cout << "{\"load_ok\":false,\"load_error\":\"Application ClassId "
                 "mismatch\",\"surfaces\":[]}\n";
    return 2;
  }
  auto application =
      Application::ptr::MakeFromThis(static_cast<Application*>(&*loaded));
  PrintInspectJson(*application, true, nullptr);
  return 0;
}

}  // namespace apptraverse
