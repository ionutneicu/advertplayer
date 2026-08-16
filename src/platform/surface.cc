#include "platform/surface.hpp"

#include <cstdlib>
#include <vector>

#if REFAPP_ENABLE_DISPMANX
#include "platform/dispmanx/surface_dispmanx.hpp"
#endif

namespace refapp {
namespace {

bool EnvironmentIsSet(const char* name) {
  const char* value = std::getenv(name);
  return value != nullptr && *value != '\0';
}

// FR-7: Wayland if WAYLAND_DISPLAY is set, else X11 if DISPLAY is set, else
// DispmanX. The fallback works because DispmanX has no window system at all --
// it composites a fullscreen layer directly, which is exactly the situation in
// which no display-server variable exists.
Backend DetectBackend() {
  if (EnvironmentIsSet("WAYLAND_DISPLAY")) return Backend::kWayland;
  if (EnvironmentIsSet("DISPLAY")) return Backend::kX11;
  return Backend::kDispmanX;
}

std::unique_ptr<Surface> Create(Backend backend) {
  switch (backend) {
    case Backend::kDispmanX:
#if REFAPP_ENABLE_DISPMANX
      return CreateDispmanxSurface();
#else
      throw SurfaceError(
          "the DispmanX backend was not compiled in; configure with "
          "-DENABLE_DISPMANX=ON on a Raspberry Pi");
#endif

    case Backend::kWayland:
      throw SurfaceError("the Wayland backend is not implemented yet (S-03)");

    case Backend::kX11:
      throw SurfaceError("the X11 backend is not implemented yet (S-03)");

    case Backend::kAuto:
      break;
  }
  throw SurfaceError("no backend selected");
}

}  // namespace

Backend ParseBackend(std::string_view name) {
  if (name.empty() || name == "auto") return Backend::kAuto;
  if (name == "wayland") return Backend::kWayland;
  if (name == "x11") return Backend::kX11;
  if (name == "dispmanx") return Backend::kDispmanX;
  throw SurfaceError("unknown backend '" + std::string(name) +
                     "'; expected one of: auto, wayland, x11, dispmanx");
}

const char* BackendName(Backend backend) {
  switch (backend) {
    case Backend::kAuto:
      return "auto";
    case Backend::kWayland:
      return "wayland";
    case Backend::kX11:
      return "x11";
    case Backend::kDispmanX:
      return "dispmanx";
  }
  return "unknown";
}

std::string CompiledBackends() {
  std::vector<std::string> names;
#if REFAPP_ENABLE_DISPMANX
  names.emplace_back("dispmanx");
#endif
  if (names.empty()) return "none";

  std::string joined = names.front();
  for (std::size_t i = 1; i < names.size(); ++i) {
    joined += ", ";
    joined += names[i];
  }
  return joined;
}

std::unique_ptr<Surface> CreateSurface(const SurfaceOptions& options) {
  const Backend backend = options.backend == Backend::kAuto
                              ? DetectBackend()
                              : options.backend;
  return Create(backend);
}

}  // namespace refapp
