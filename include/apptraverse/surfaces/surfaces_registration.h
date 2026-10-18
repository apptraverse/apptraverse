#ifndef APPTRAVERSE_SURFACES_REGISTRATION_H_
#define APPTRAVERSE_SURFACES_REGISTRATION_H_

namespace apptraverse {

// Model nodes/events + platform-neutral Surface chrome presenters.
void EnsureSurfacesRegistration();

// Back-compat with demo/tests that called the model-only entry point.
inline void EnsureSurfacesModelRegistration() { EnsureSurfacesRegistration(); }

}  // namespace apptraverse

#endif  // APPTRAVERSE_SURFACES_REGISTRATION_H_
