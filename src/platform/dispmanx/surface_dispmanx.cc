#include "platform/dispmanx/surface_dispmanx.hpp"

// bcm_host.h must precede the EGL headers: it is what defines
// EGL_DISPMANX_WINDOW_T, via the Broadcom eglplatform.h.
#include <bcm_host.h>

#include <EGL/egl.h>
#include <GLES2/gl2.h>

#include <cstdio>
#include <string>

namespace refapp {
namespace {

std::string EglErrorText() {
  const EGLint error = eglGetError();
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "EGL error 0x%04x", error);
  return buffer;
}

// Ported from the 2017 implementation
// (platform-lib/platform-egl-context-dispmanx.c), with its assertions replaced
// by errors and its teardown -- which was a TODO -- actually written.
class DispmanxSurface final : public Surface {
 public:
  DispmanxSurface();
  ~DispmanxSurface() override;

  void MakeCurrent() override;
  void SwapBuffers() noexcept override;

  SurfaceGeometry geometry() const noexcept override { return geometry_; }
  Backend backend() const noexcept override { return Backend::kDispmanX; }

 private:
  void OpenDisplay();
  void CreateElement();
  void CreateContext();

  SurfaceGeometry geometry_;

  EGLDisplay display_ = EGL_NO_DISPLAY;
  EGLContext context_ = EGL_NO_CONTEXT;
  EGLSurface egl_surface_ = EGL_NO_SURFACE;
  EGLConfig config_ = nullptr;

  DISPMANX_DISPLAY_HANDLE_T dispmanx_display_ = 0;
  DISPMANX_ELEMENT_HANDLE_T dispmanx_element_ = 0;
  EGL_DISPMANX_WINDOW_T native_window_{};
};

DispmanxSurface::DispmanxSurface() {
  // Safe to call more than once; the Broadcom host library reference counts.
  bcm_host_init();

  OpenDisplay();
  CreateContext();
  CreateElement();

  egl_surface_ =
      eglCreateWindowSurface(display_, config_, &native_window_, nullptr);
  if (egl_surface_ == EGL_NO_SURFACE) {
    throw SurfaceError("eglCreateWindowSurface failed: " + EglErrorText());
  }
}

void DispmanxSurface::OpenDisplay() {
  display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  if (display_ == EGL_NO_DISPLAY) {
    throw SurfaceError("no EGL display: " + EglErrorText());
  }
  if (eglInitialize(display_, nullptr, nullptr) == EGL_FALSE) {
    throw SurfaceError("eglInitialize failed: " + EglErrorText());
  }

  std::uint32_t width = 0;
  std::uint32_t height = 0;
  if (graphics_get_display_size(0 /* LCD */, &width, &height) < 0) {
    throw SurfaceError("graphics_get_display_size failed");
  }
  geometry_ = SurfaceGeometry{width, height};
}

void DispmanxSurface::CreateContext() {
  // GLES2 (FR-10). No depth buffer is requested: this renderer is 2D
  // (FR-11), and the legacy Broadcom stack has little memory to spare.
  static const EGLint kConfigAttributes[] = {
      EGL_RED_SIZE,     8,
      EGL_GREEN_SIZE,   8,
      EGL_BLUE_SIZE,    8,
      EGL_ALPHA_SIZE,   8,
      EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
      EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
      EGL_NONE};
  static const EGLint kContextAttributes[] = {EGL_CONTEXT_CLIENT_VERSION, 2,
                                              EGL_NONE};

  EGLint config_count = 0;
  if (eglChooseConfig(display_, kConfigAttributes, &config_, 1,
                      &config_count) == EGL_FALSE ||
      config_count == 0) {
    throw SurfaceError("no suitable EGL config: " + EglErrorText());
  }
  if (eglBindAPI(EGL_OPENGL_ES_API) == EGL_FALSE) {
    throw SurfaceError("eglBindAPI failed: " + EglErrorText());
  }

  context_ =
      eglCreateContext(display_, config_, EGL_NO_CONTEXT, kContextAttributes);
  if (context_ == EGL_NO_CONTEXT) {
    throw SurfaceError("eglCreateContext failed: " + EglErrorText());
  }
}

void DispmanxSurface::CreateElement() {
  // Source rectangle is in 16.16 fixed point; destination is in pixels.
  VC_RECT_T destination;
  destination.x = 0;
  destination.y = 0;
  destination.width = static_cast<std::int32_t>(geometry_.width);
  destination.height = static_cast<std::int32_t>(geometry_.height);

  VC_RECT_T source;
  source.x = 0;
  source.y = 0;
  source.width = static_cast<std::int32_t>(geometry_.width) << 16;
  source.height = static_cast<std::int32_t>(geometry_.height) << 16;

  dispmanx_display_ = vc_dispmanx_display_open(0 /* LCD */);
  if (dispmanx_display_ == 0) {
    throw SurfaceError("vc_dispmanx_display_open failed");
  }

  DISPMANX_UPDATE_HANDLE_T update = vc_dispmanx_update_start(0);
  if (update == 0) {
    throw SurfaceError("vc_dispmanx_update_start failed");
  }

  dispmanx_element_ = vc_dispmanx_element_add(
      update, dispmanx_display_, 0 /* layer */, &destination, 0 /* src */,
      &source, DISPMANX_PROTECTION_NONE, nullptr /* alpha */,
      nullptr /* clamp */, DISPMANX_NO_ROTATE);
  if (dispmanx_element_ == 0) {
    vc_dispmanx_update_submit_sync(update);
    throw SurfaceError("vc_dispmanx_element_add failed");
  }

  native_window_.element = dispmanx_element_;
  native_window_.width = static_cast<int>(geometry_.width);
  native_window_.height = static_cast<int>(geometry_.height);

  vc_dispmanx_update_submit_sync(update);
}

void DispmanxSurface::MakeCurrent() {
  if (eglMakeCurrent(display_, egl_surface_, egl_surface_, context_) ==
      EGL_FALSE) {
    throw SurfaceError("eglMakeCurrent failed: " + EglErrorText());
  }
}

void DispmanxSurface::SwapBuffers() noexcept {
  eglSwapBuffers(display_, egl_surface_);
}

// The teardown the 2017 implementation left as a TODO. It matters here: the
// scene player creates and destroys surfaces across a process lifetime, and a
// leaked DispmanX element stays composited on screen after the process exits.
DispmanxSurface::~DispmanxSurface() {
  if (display_ != EGL_NO_DISPLAY) {
    eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (egl_surface_ != EGL_NO_SURFACE) {
      eglDestroySurface(display_, egl_surface_);
    }
    if (context_ != EGL_NO_CONTEXT) {
      eglDestroyContext(display_, context_);
    }
    eglTerminate(display_);
  }

  if (dispmanx_element_ != 0) {
    DISPMANX_UPDATE_HANDLE_T update = vc_dispmanx_update_start(0);
    if (update != 0) {
      vc_dispmanx_element_remove(update, dispmanx_element_);
      vc_dispmanx_update_submit_sync(update);
    }
  }
  if (dispmanx_display_ != 0) {
    vc_dispmanx_display_close(dispmanx_display_);
  }
}

}  // namespace

std::unique_ptr<Surface> CreateDispmanxSurface() {
  return std::make_unique<DispmanxSurface>();
}

}  // namespace refapp
