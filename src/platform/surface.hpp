#ifndef REFAPP_SRC_PLATFORM_SURFACE_HPP_
#define REFAPP_SRC_PLATFORM_SURFACE_HPP_

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

// The surface layer: an EGL display, a GLES2 context and something to present
// to. One interface, one implementation per platform -- DispmanX here, X11 and
// Wayland alongside it (FR-1, FR-2, FR-126). Everything above this file is
// identical on all three, which is the point (risk R-4).
//
// EGL throughout, never GLX (FR-5). GLX would serve X11 only and forfeit both
// the Raspberry Pi and any future WebGL target.

namespace refapp {

// Raised when a surface cannot be created. Construction is an initialisation
// path, where exceptions are permitted (coding-style §2 rule 5).
class SurfaceError : public std::runtime_error {
 public:
  explicit SurfaceError(const std::string& message)
      : std::runtime_error(message) {}
};

enum class Backend {
  kAuto,      // Choose per FR-7.
  kWayland,   // Not yet implemented -- story S-03.
  kX11,       // Not yet implemented -- story S-03.
  kDispmanX,
};

// Parses a backend name for the command-line override of FR-7.
// Returns kAuto for an empty string; throws for anything unrecognised.
Backend ParseBackend(std::string_view name);

const char* BackendName(Backend backend);

struct SurfaceOptions {
  Backend backend = Backend::kAuto;
};

// Size of the drawable, in pixels. Held by the application for its viewport
// and aspect calculations; never published to plugins (FR-13).
struct SurfaceGeometry {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
};

class Surface {
 public:
  virtual ~Surface() = default;

  Surface(const Surface&) = delete;
  Surface& operator=(const Surface&) = delete;

  // Binds the context to the calling thread.
  virtual void MakeCurrent() = 0;

  // Presents the back buffer. Called once per frame, so it must not throw
  // (coding-style §2 rule 1).
  virtual void SwapBuffers() noexcept = 0;

  virtual SurfaceGeometry geometry() const noexcept = 0;
  virtual Backend backend() const noexcept = 0;

 protected:
  Surface() = default;
};

// Creates a surface using the requested backend, or the first available one in
// the order of FR-7: Wayland if WAYLAND_DISPLAY is set, else X11 if DISPLAY is
// set, else DispmanX.
//
// Throws SurfaceError if the requested backend was not compiled in, or if no
// backend can be brought up.
std::unique_ptr<Surface> CreateSurface(const SurfaceOptions& options);

// Backends compiled into this binary, for diagnostics: "dispmanx", or
// "none" if the build excluded all of them.
std::string CompiledBackends();

}  // namespace refapp

#endif  // REFAPP_SRC_PLATFORM_SURFACE_HPP_
