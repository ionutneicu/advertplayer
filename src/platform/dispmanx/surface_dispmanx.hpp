#ifndef REFAPP_SRC_PLATFORM_DISPMANX_SURFACE_DISPMANX_HPP_
#define REFAPP_SRC_PLATFORM_DISPMANX_SURFACE_DISPMANX_HPP_

#include <memory>

#include "platform/surface.hpp"

// Compiled only when ENABLE_DISPMANX is on, which requires the Broadcom
// headers and libbcm_host -- present on a Raspberry Pi running the legacy
// graphics stack and nowhere else (FR-127).

namespace refapp {

// Brings up a fullscreen DispmanX element and an EGL surface on it.
// Throws SurfaceError on any failure.
std::unique_ptr<Surface> CreateDispmanxSurface();

}  // namespace refapp

#endif  // REFAPP_SRC_PLATFORM_DISPMANX_SURFACE_DISPMANX_HPP_
