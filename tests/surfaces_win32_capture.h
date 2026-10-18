#ifndef APPTRAVERSE_SURFACES_WIN32_CAPTURE_H_
#define APPTRAVERSE_SURFACES_WIN32_CAPTURE_H_

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

namespace apptraverse::test {

inline bool SaveWindowClientBmp(HWND hwnd, std::filesystem::path const& path) {
  if (hwnd == nullptr || !IsWindow(hwnd)) {
    return false;
  }
  RECT client{};
  if (GetClientRect(hwnd, &client) == 0) {
    return false;
  }
  int const width = client.right - client.left;
  int const height = client.bottom - client.top;
  if (width <= 0 || height <= 0) {
    return false;
  }

  HDC const window_dc = GetDC(hwnd);
  if (window_dc == nullptr) {
    return false;
  }
  HDC const mem_dc = CreateCompatibleDC(window_dc);
  if (mem_dc == nullptr) {
    ReleaseDC(hwnd, window_dc);
    return false;
  }
  HBITMAP const bitmap =
      CreateCompatibleBitmap(window_dc, width, height);
  if (bitmap == nullptr) {
    DeleteDC(mem_dc);
    ReleaseDC(hwnd, window_dc);
    return false;
  }
  HGDIOBJ const old = SelectObject(mem_dc, bitmap);
  if (PrintWindow(hwnd, mem_dc, PW_CLIENTONLY) == 0) {
    SelectObject(mem_dc, old);
    DeleteObject(bitmap);
    DeleteDC(mem_dc);
    ReleaseDC(hwnd, window_dc);
    return false;
  }

  BITMAPINFOHEADER bi{};
  bi.biSize = sizeof(bi);
  bi.biWidth = width;
  bi.biHeight = -height;
  bi.biPlanes = 1;
  bi.biBitCount = 32;
  bi.biCompression = BI_RGB;
  std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) *
                                   static_cast<std::size_t>(height) * 4);
  if (GetDIBits(mem_dc, bitmap, 0, static_cast<UINT>(height), pixels.data(),
                reinterpret_cast<BITMAPINFO*>(&bi),
                DIB_RGB_COLORS) == 0) {
    SelectObject(mem_dc, old);
    DeleteObject(bitmap);
    DeleteDC(mem_dc);
    ReleaseDC(hwnd, window_dc);
    return false;
  }

  SelectObject(mem_dc, old);
  DeleteObject(bitmap);
  DeleteDC(mem_dc);
  ReleaseDC(hwnd, window_dc);

  std::filesystem::create_directories(path.parent_path());
  std::ofstream out{path, std::ios::binary};
  if (!out) {
    return false;
  }
  BITMAPFILEHEADER fh{};
  fh.bfType = 0x4D42;
  fh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
  fh.bfSize = fh.bfOffBits + static_cast<DWORD>(pixels.size());
  out.write(reinterpret_cast<char const*>(&fh), sizeof(fh));
  out.write(reinterpret_cast<char const*>(&bi), sizeof(bi));
  out.write(reinterpret_cast<char const*>(pixels.data()),
            static_cast<std::streamsize>(pixels.size()));
  return static_cast<bool>(out);
}

}  // namespace apptraverse::test

#endif  // APPTRAVERSE_SURFACES_WIN32_CAPTURE_H_
