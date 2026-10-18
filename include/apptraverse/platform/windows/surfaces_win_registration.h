#ifndef APPTRAVERSE_SURFACES_WIN_REGISTRATION_H_
#define APPTRAVERSE_SURFACES_WIN_REGISTRATION_H_

namespace apptraverse {

void EnsureWin32SurfacesPresenterRegistration();

inline void EnsureWin32SurfacePresenterRegistration() {
  EnsureWin32SurfacesPresenterRegistration();
}

}  // namespace apptraverse

#endif  // APPTRAVERSE_SURFACES_WIN_REGISTRATION_H_
