#ifndef APPTRAVERSE_DYNAMIC_WIN_PRESENTERS_H_
#define APPTRAVERSE_DYNAMIC_WIN_PRESENTERS_H_

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#ifdef RegisterClass
#  undef RegisterClass
#endif

#include "apptraverse/object_macros.h"

#include "dynamic_model.h"

namespace apptraverse {

inline wchar_t const kDynamicMainClass[] = L"AppTraverseDynamicObjectsMain";
inline wchar_t const kDynamicItemListClass[] = L"AppTraverseDynamicItemList";
inline wchar_t const kDynamicMainTitle[] = L"Dynamic Objects";
inline constexpr int kAddButtonId = 1001;
inline constexpr int kRemoveButtonId = 1002;

void RegisterDynamicWin32Classes();
void UnregisterDynamicWin32Classes();

class Win32MainWindowPresenter : public MainWindowPresenter {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::dynamic::Win32MainWindowPresenter",
      Win32MainWindowPresenter, MainWindowPresenter, 0)

 protected:
  Win32MainWindowPresenter() = default;

 public:
  explicit Win32MainWindowPresenter(ae::ObjProp prop)
      : MainWindowPresenter{prop} {}

  AE_OBJECT_REFLECT()

  void OnLoad() override;
  void OnModelChanged() override;
  void OnUnload() override;

  HWND hwnd{nullptr};

  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam,
                                  LPARAM lparam);
};

class Win32AddItemPresenter : public AddItemPresenter {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::dynamic::Win32AddItemPresenter",
      Win32AddItemPresenter, AddItemPresenter, 0)

 protected:
  Win32AddItemPresenter() = default;

 public:
  explicit Win32AddItemPresenter(ae::ObjProp prop) : AddItemPresenter{prop} {}

  AE_OBJECT_REFLECT()

  bool ReadyForPresentation() const override;
  void OnLoad() override;
  void OnModelChanged() override;
  void OnUnload() override;

  HWND hwnd{nullptr};
};

class Win32ItemListPresenter : public ItemListPresenter {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::dynamic::Win32ItemListPresenter",
      Win32ItemListPresenter, ItemListPresenter, 0)

 protected:
  Win32ItemListPresenter() = default;

 public:
  explicit Win32ItemListPresenter(ae::ObjProp prop)
      : ItemListPresenter{prop} {}

  AE_OBJECT_REFLECT()

  bool ReadyForPresentation() const override;
  void OnLoad() override;
  void OnModelChanged() override;
  void OnUnload() override;

  HWND hwnd{nullptr};

  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam,
                                  LPARAM lparam);
};

class Win32ItemPresenter : public ItemPresenter {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::example::dynamic::Win32ItemPresenter",
                           Win32ItemPresenter, ItemPresenter, 0)

 protected:
  Win32ItemPresenter() = default;

 public:
  explicit Win32ItemPresenter(ae::ObjProp prop) : ItemPresenter{prop} {}

  AE_OBJECT_REFLECT()

  bool ReadyForPresentation() const override;
  void OnLoad() override;
  void OnModelChanged() override;
  void OnUnload() override;

  HWND hwnd{nullptr};
  HWND remove_button{nullptr};

 private:
  int LiveIndex() const;
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_DYNAMIC_WIN_PRESENTERS_H_
