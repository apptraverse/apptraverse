#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

#include "aether-objects/obj/domain.h"
#include "aether-objects/obj/obj.h"

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/object_macros.h"

// Two independent EXEs, one ClassId, independent version slots.
#ifndef APPTRAVERSE_WINDOW_EXE_REVISION
#error APPTRAVERSE_WINDOW_EXE_REVISION is required
#endif
static_assert(APPTRAVERSE_WINDOW_EXE_REVISION == 0 ||
              APPTRAVERSE_WINDOW_EXE_REVISION == 1 ||
              APPTRAVERSE_WINDOW_EXE_REVISION == 2);

namespace apptraverse::test::window_layers {

constexpr std::uint32_t kWindowId = 92001;

class Window : public ae::Obj {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::window_layers::Window", Window,
                           ae::Obj, APPTRAVERSE_WINDOW_EXE_REVISION)

 protected:
  Window() = default;

 public:
  explicit Window(ae::ObjProp prop) : Obj{prop} {}

#if APPTRAVERSE_WINDOW_EXE_REVISION == 2
  AE_OBJECT_REFLECT(AE_MMBR(x), AE_MMBR(y), AE_MMBR(width), AE_MMBR(height),
                    AE_MMBR(border_width))
#elif APPTRAVERSE_WINDOW_EXE_REVISION == 1
  AE_OBJECT_REFLECT(AE_MMBR(x), AE_MMBR(y), AE_MMBR(width), AE_MMBR(height))
#else
  AE_OBJECT_REFLECT(AE_MMBR(x), AE_MMBR(y))
#endif

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, x, y);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, x, y);
  }

#if APPTRAVERSE_WINDOW_EXE_REVISION >= 1
  template <typename Dnv>
  void Load(ae::Version<1>, Dnv& dnv) {
    dnv(base_, width, height);
  }

  template <typename Dnv>
  void Save(ae::Version<1>, Dnv& dnv) const {
    dnv(base_, width, height);
  }
#endif

#if APPTRAVERSE_WINDOW_EXE_REVISION == 2
  template <typename Dnv>
  void Load(ae::Version<2>, Dnv& dnv) {
    dnv(base_, border_width);
  }

  template <typename Dnv>
  void Save(ae::Version<2>, Dnv& dnv) const {
    dnv(base_, border_width);
  }
#endif

  std::int32_t x{0};
  std::int32_t y{0};
#if APPTRAVERSE_WINDOW_EXE_REVISION >= 1
  std::uint32_t width{0};
  std::uint32_t height{0};
#endif
#if APPTRAVERSE_WINDOW_EXE_REVISION == 2
  std::uint32_t border_width;
#endif
};

APPTRAVERSE_REGISTER(Window);

int Run(int argc, char** argv) {
  std::filesystem::path state_dir;
  std::string operation;
  for (int i = 1; i < argc; ++i) {
    std::string_view const argument{argv[i]};
    if (argument == "--state-dir" && i + 1 < argc) {
      state_dir = argv[++i];
    } else if (argument == "--op" && i + 1 < argc) {
      operation = argv[++i];
    } else {
      throw std::runtime_error("invalid or incomplete argument");
    }
  }
  if (state_dir.empty() ||
      (operation != "create" && operation != "package" &&
       operation != "inspect" && operation != "move" &&
       operation != "resize" && operation != "move-resize" &&
       operation != "change-border")) {
    throw std::runtime_error(
        "usage: --state-dir PATH --op "
        "create|package|inspect|move|resize|move-resize|change-border");
  }

  bool const new_state = operation == "create" || operation == "package";
  if (new_state) {
    if (std::filesystem::exists(state_dir) &&
        (!std::filesystem::is_directory(state_dir) ||
         !std::filesystem::is_empty(state_dir))) {
      throw std::runtime_error("new state/package directory must be empty");
    }
  } else if (!std::filesystem::is_directory(state_dir)) {
    throw std::runtime_error("state directory does not exist");
  }

#if APPTRAVERSE_WINDOW_EXE_REVISION == 0
  if (operation == "package" || operation == "resize" ||
      operation == "move-resize" || operation == "change-border") {
    throw std::runtime_error("v0 EXE does not support v1 fields");
  }
#else
  if (operation == "create") {
    throw std::runtime_error("create v0 user state with EXE A");
  }
#if APPTRAVERSE_WINDOW_EXE_REVISION == 1
  if (operation == "change-border") {
    throw std::runtime_error("v1 EXE does not support v2 fields");
  }
#endif
  if (operation != "package" &&
      !DirectoryDomainStorageLayerFileExists(
          state_dir, ae::DomainQuery{ae::ObjId{kWindowId}, Window::kClassId, 1})) {
    std::cerr << "MISSING_STATE_LAYER class=" << Window::kClassId
              << " version=1 (must be provided by packaged state)\n";
    return 2;
  }
#if APPTRAVERSE_WINDOW_EXE_REVISION == 2
  if (operation != "package" &&
      !DirectoryDomainStorageLayerFileExists(
          state_dir, ae::DomainQuery{ae::ObjId{kWindowId}, Window::kClassId, 2})) {
    std::cerr << "MISSING_STATE_LAYER class=" << Window::kClassId
              << " version=2 (must be provided by packaged state)\n";
    return 2;
  }
#endif
#endif

  EnsureObjectRegistration();
  DirectoryDomainStorage storage{state_dir};
  ae::Domain domain{storage};

  ae::Ptr<Window> window;
  if (new_state) {
    auto created = Window::ptr::Create(
        ae::CreateWith{domain}.with_id(ae::ObjId{kWindowId}));
    window = created.Load();
    if (operation == "create") {
      window->x = 10;
      window->y = 20;
    }
#if APPTRAVERSE_WINDOW_EXE_REVISION == 1
    if (operation == "package") {
      // New fields arrive in a serialized v1 state, never from Load(v0).
      window->width = 640;
      window->height = 480;
    }
#endif
#if APPTRAVERSE_WINDOW_EXE_REVISION == 2
    if (operation == "package") {
      window->border_width = 4;
    }
#endif
  } else {
    ae::DomainGraph load_graph{&domain};
    ae::Ptr<ae::Obj> loaded;
    if (!load_graph.LoadRoot(ae::ObjId{kWindowId}, loaded).IsOk() || !loaded ||
        loaded->GetClassId() != Window::kClassId) {
      throw std::runtime_error("LoadRoot failed or Window ClassId changed");
    }
    window = ae::Ptr<Window>{loaded};
  }

  if (window->obj_id.id() != kWindowId ||
      window->GetClassId() != Window::kClassId) {
    throw std::runtime_error("Window identity changed");
  }
  if (operation == "move") {
#if APPTRAVERSE_WINDOW_EXE_REVISION == 0
    window->x += 7;
    window->y += 9;
#else
    window->x += 13;
    window->y += 17;
#endif
  }
#if APPTRAVERSE_WINDOW_EXE_REVISION == 2
  if (operation == "change-border") {
    window->border_width += 2;
  }
#endif
#if APPTRAVERSE_WINDOW_EXE_REVISION == 1
  if (operation == "resize") {
    window->width += 100;
    window->height += 50;
  }
  if (operation == "move-resize") {
    window->x += 13;
    window->y += 17;
    window->width += 100;
    window->height += 50;
  }
#endif

  if (operation != "inspect") {
    storage.BeginSaveSession();
    ae::DomainGraph save_graph{&domain};
#if APPTRAVERSE_WINDOW_EXE_REVISION == 2
    ae::seri::SeriResult const result =
        operation == "package"
            ? save_graph.SaveVersion(ae::Version<2>{}, *window,
                                      ae::ObjId{kWindowId})
            : save_graph.SaveRoot(window, ae::ObjId{kWindowId});
#elif APPTRAVERSE_WINDOW_EXE_REVISION == 1
    ae::seri::SeriResult const result =
        operation == "package"
            ? save_graph.SaveVersion(ae::Version<1>{}, *window,
                                      ae::ObjId{kWindowId})
            : save_graph.SaveRoot(window, ae::ObjId{kWindowId});
#else
    ae::seri::SeriResult const result =
        save_graph.SaveRoot(window, ae::ObjId{kWindowId});
#endif
    if (!result.IsOk() || storage.SaveSessionHadFailure()) {
      throw std::runtime_error("saving Window state failed");
    }
  }

  std::cout << "{\"exe\":\"";
  if constexpr (APPTRAVERSE_WINDOW_EXE_REVISION == 0) {
    std::cout << 'A';
  } else if constexpr (APPTRAVERSE_WINDOW_EXE_REVISION == 1) {
    std::cout << 'B';
  } else {
    std::cout << 'C';
  }
  std::cout << "\",\"class_id\":" << Window::kClassId
            << ",\"object_id\":" << window->obj_id.id()
            << ",\"x\":" << window->x << ",\"y\":" << window->y;
#if APPTRAVERSE_WINDOW_EXE_REVISION >= 1
  std::cout << ",\"width\":" << window->width
            << ",\"height\":" << window->height;
#endif
#if APPTRAVERSE_WINDOW_EXE_REVISION == 2
  std::cout << ",\"border_width\":" << window->border_width;
#endif
  std::cout << "}\n";
  return 0;
}

}  // namespace apptraverse::test::window_layers

int main(int argc, char** argv) {
  try {
    return apptraverse::test::window_layers::Run(argc, argv);
  } catch (std::exception const& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
