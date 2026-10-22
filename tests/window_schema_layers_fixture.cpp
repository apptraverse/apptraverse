#include <cstdint>

#include <filesystem>

#include <iostream>

#include <memory>

#include <stdexcept>

#include <string>

#include <string_view>



#include "aether-objects/domain_storage/ram_domain_storage.h"

#include "aether-objects/obj/domain.h"

#include "aether-objects/obj/obj.h"



#include "apptraverse/directory_domain_storage.h"

#include "apptraverse/object_macros.h"

#include "apptraverse/overlay_domain_storage.h"



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



#if APPTRAVERSE_WINDOW_EXE_REVISION == 1

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

  void Load(ae::Version<1>, Dnv& dnv) {

    dnv(base_, width, height);

  }



  template <typename Dnv>

  void Save(ae::Version<1>, Dnv& dnv) const {

    dnv(base_, width, height);

  }



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



#if APPTRAVERSE_WINDOW_EXE_REVISION == 0 || APPTRAVERSE_WINDOW_EXE_REVISION == 1 || \
    APPTRAVERSE_WINDOW_EXE_REVISION == 2

constexpr std::uint8_t kEmbeddedWindowV0Layer[] = {

    0x0A, 0x00, 0x00, 0x00, 0x14, 0x00, 0x00, 0x00,

};



void WriteEmbeddedLayer(ae::RamDomainStorage& storage,

                        ae::DomainQuery const& query,

                        std::uint8_t const* data, std::size_t size) {

  std::unique_ptr<ae::IDomainStorageWriter> writer = storage.Store(query);

  if (!writer->Write(ae::seri::DataWriteTag{data, size})) {

    throw std::runtime_error("embedded layer write failed");

  }

}

#endif



#if APPTRAVERSE_WINDOW_EXE_REVISION == 1 || APPTRAVERSE_WINDOW_EXE_REVISION == 2

constexpr std::uint8_t kEmbeddedWindowV1Layer[] = {

    0x80, 0x02, 0x00, 0x00, 0xE0, 0x01, 0x00, 0x00,

};

#endif



#if APPTRAVERSE_WINDOW_EXE_REVISION == 2

constexpr std::uint8_t kEmbeddedWindowV2Layer[] = {

    0x04, 0x00, 0x00, 0x00,

};

#endif



int Run(int argc, char** argv) {

  std::filesystem::path state_dir;

  std::filesystem::path backing_dir;

  bool embedded_backing = false;

  std::string operation;

  for (int i = 1; i < argc; ++i) {

    std::string_view const argument{argv[i]};

    if (argument == "--state-dir" && i + 1 < argc) {

      state_dir = argv[++i];

    } else if (argument == "--backing-dir" && i + 1 < argc) {

      backing_dir = argv[++i];

    } else if (argument == "--embedded-backing") {

      embedded_backing = true;

    } else if (argument == "--op" && i + 1 < argc) {

      operation = argv[++i];

    } else {

      throw std::runtime_error("invalid or incomplete argument");

    }

  }

  if (embedded_backing && !backing_dir.empty()) {

    throw std::runtime_error(

        "--embedded-backing and --backing-dir are mutually exclusive");

  }

  if (state_dir.empty() ||

      (operation != "create" && operation != "package" &&

       operation != "inspect" && operation != "move" &&

       operation != "resize" && operation != "move-resize" &&

       operation != "change-border" && operation != "reconcile")) {

    throw std::runtime_error(

        "usage: --state-dir PATH --op "

        "create|package|inspect|move|resize|move-resize|change-border|reconcile");

  }



  bool const new_state = operation == "create" || operation == "package";

  if (new_state) {

    if (std::filesystem::exists(state_dir) &&

        (!std::filesystem::is_directory(state_dir) ||

         !std::filesystem::is_empty(state_dir))) {

      throw std::runtime_error("new state/package directory must be empty");

    }

  } else if (!std::filesystem::is_directory(state_dir)) {

    bool const embedded_empty_state_ok =

        embedded_backing &&

        (operation == "inspect" || operation == "move"

#if APPTRAVERSE_WINDOW_EXE_REVISION == 1

         || operation == "resize" || operation == "move-resize"

#endif

#if APPTRAVERSE_WINDOW_EXE_REVISION == 2

         || operation == "resize" || operation == "move-resize" ||

         operation == "change-border"

#endif

        );

    if (!embedded_empty_state_ok) {

      throw std::runtime_error("state directory does not exist");

    }

  }



#if APPTRAVERSE_WINDOW_EXE_REVISION == 0

  if (operation == "package" || operation == "resize" ||

      operation == "move-resize" || operation == "change-border" ||

      operation == "reconcile") {

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

  auto const layer_exists = [&](std::uint8_t version) {

    ae::DomainQuery const query{ae::ObjId{kWindowId}, Window::kClassId, version};

    if (DirectoryDomainStorageLayerFileExists(state_dir, query)) {

      return true;

    }

    if (!backing_dir.empty() &&

        DirectoryDomainStorageLayerFileExists(backing_dir, query)) {

      return true;

    }

#if APPTRAVERSE_WINDOW_EXE_REVISION == 1

    if (embedded_backing && version <= 1) {

      return true;

    }

#endif

#if APPTRAVERSE_WINDOW_EXE_REVISION == 2

    if (embedded_backing && version <= 2) {

      return true;

    }

#endif

    return false;

  };

  if (operation != "package" && !layer_exists(1)) {

    std::cerr << "MISSING_STATE_LAYER class=" << Window::kClassId

              << " version=1 (must be provided by packaged state)\n";

    return 2;

  }

#if APPTRAVERSE_WINDOW_EXE_REVISION == 2

  if (operation != "package" && !layer_exists(2)) {

    std::cerr << "MISSING_STATE_LAYER class=" << Window::kClassId

              << " version=2 (must be provided by packaged state)\n";

    return 2;

  }

#endif

#endif



  if (!backing_dir.empty() && !std::filesystem::is_directory(backing_dir)) {

    throw std::runtime_error("backing directory does not exist");

  }



  EnsureObjectRegistration();

  DirectoryDomainStorage user_storage{state_dir};

  std::unique_ptr<DirectoryDomainStorage> backing_storage;

  std::unique_ptr<ae::RamDomainStorage> embedded_storage;

  std::unique_ptr<OverlayDomainStorage> overlay_storage;

  ae::IDomainStorage* domain_storage = &user_storage;

  if (!backing_dir.empty()) {

    backing_storage =

        std::make_unique<DirectoryDomainStorage>(backing_dir);

    overlay_storage = std::make_unique<OverlayDomainStorage>(

        user_storage, *backing_storage);

    domain_storage = overlay_storage.get();

  } else if (embedded_backing) {

    embedded_storage = std::make_unique<ae::RamDomainStorage>();

    ae::DomainQuery const v0_query{ae::ObjId{kWindowId}, Window::kClassId, 0};

    ae::DomainQuery const obj_v0_query{ae::ObjId{kWindowId}, ae::Obj::kClassId, 0};

    WriteEmbeddedLayer(*embedded_storage, obj_v0_query, nullptr, 0);

    WriteEmbeddedLayer(*embedded_storage, v0_query, kEmbeddedWindowV0Layer,

                       sizeof(kEmbeddedWindowV0Layer));

#if APPTRAVERSE_WINDOW_EXE_REVISION == 1 || APPTRAVERSE_WINDOW_EXE_REVISION == 2

    ae::DomainQuery const v1_query{ae::ObjId{kWindowId}, Window::kClassId, 1};

    WriteEmbeddedLayer(*embedded_storage, v1_query, kEmbeddedWindowV1Layer,

                       sizeof(kEmbeddedWindowV1Layer));

#endif

#if APPTRAVERSE_WINDOW_EXE_REVISION == 2

    ae::DomainQuery const v2_query{ae::ObjId{kWindowId}, Window::kClassId, 2};

    WriteEmbeddedLayer(*embedded_storage, v2_query, kEmbeddedWindowV2Layer,

                       sizeof(kEmbeddedWindowV2Layer));

#endif

    overlay_storage = std::make_unique<OverlayDomainStorage>(

        user_storage, *embedded_storage);

    domain_storage = overlay_storage.get();

  }

  ae::Domain domain{*domain_storage};



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

#if APPTRAVERSE_WINDOW_EXE_REVISION >= 1

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

  if (operation == "reconcile") {

    window->width = static_cast<std::uint32_t>(1000 - window->x);

  }

#endif

#if APPTRAVERSE_WINDOW_EXE_REVISION == 2

  if (operation == "change-border") {

    window->border_width += 2;

  }

#endif



  if (operation != "inspect") {

    if (embedded_backing) {

      ae::DomainQuery const obj_v0_query{ae::ObjId{kWindowId}, ae::Obj::kClassId,

                                         0};

      if (!DirectoryDomainStorageLayerFileExists(state_dir, obj_v0_query)) {

        std::unique_ptr<ae::IDomainStorageWriter> obj_writer =

            user_storage.Store(obj_v0_query);

        if (!obj_writer->Write(ae::seri::DataWriteTag{nullptr, 0})) {

          throw std::runtime_error("obj v0 seed write failed");

        }

      }

    }

    user_storage.BeginSaveSession();

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

    if (!result.IsOk() || user_storage.SaveSessionHadFailure()) {

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


