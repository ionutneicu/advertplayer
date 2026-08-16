// Minimal renderer: bring up a surface, clear it, present.
//
// Story S-02 needs one thing on screen to prove the DispmanX backend works.
// It deliberately does no more than that -- no plugins, no scenes, no resource
// manager. Those arrive in S-04 onwards.

#include <GLES2/gl2.h>

#include <cstdlib>
#include <cstring>
#include <exception>
#include <iostream>
#include <string>

#include "platform/surface.hpp"

namespace {

struct Arguments {
  refapp::Backend backend = refapp::Backend::kAuto;
  int frames = 0;  // 0 means run until interrupted.
};

void PrintUsage() {
  std::cout << "usage: refapp [--backend auto|wayland|x11|dispmanx]"
               " [--frames N]\n"
               "  --backend  override the automatic choice (FR-7)\n"
               "  --frames   present N frames and exit; 0 runs forever\n";
}

// Throws on bad input; argument parsing is an initialisation path.
Arguments ParseArguments(int argc, char** argv) {
  Arguments arguments;
  for (int i = 1; i < argc; ++i) {
    const std::string flag = argv[i];
    if (flag == "--help" || flag == "-h") {
      PrintUsage();
      std::exit(EXIT_SUCCESS);
    }
    if (i + 1 >= argc) {
      throw std::runtime_error("missing value for " + flag);
    }
    const std::string value = argv[++i];
    if (flag == "--backend") {
      arguments.backend = refapp::ParseBackend(value);
    } else if (flag == "--frames") {
      arguments.frames = std::stoi(value);
    } else {
      throw std::runtime_error("unknown option " + flag);
    }
  }
  return arguments;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const Arguments arguments = ParseArguments(argc, argv);

    std::cout << "compiled backends: " << refapp::CompiledBackends() << "\n";

    refapp::SurfaceOptions options;
    options.backend = arguments.backend;
    std::unique_ptr<refapp::Surface> surface = refapp::CreateSurface(options);
    surface->MakeCurrent();

    const refapp::SurfaceGeometry geometry = surface->geometry();
    std::cout << "backend    : " << refapp::BackendName(surface->backend())
              << "\n"
              << "surface    : " << geometry.width << "x" << geometry.height
              << "\n"
              << "GL_VERSION : " << glGetString(GL_VERSION) << "\n"
              << "GL_RENDERER: " << glGetString(GL_RENDERER) << "\n";

    glViewport(0, 0, static_cast<GLsizei>(geometry.width),
               static_cast<GLsizei>(geometry.height));

    // Premultiplied alpha (FR-91): the blend function the whole renderer uses.
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    for (int frame = 0; arguments.frames == 0 || frame < arguments.frames;
         ++frame) {
      // Something obviously non-black, so "it ran" and "it drew" are
      // distinguishable on a screen.
      glClearColor(0.10f, 0.25f, 0.45f, 1.0f);
      glClear(GL_COLOR_BUFFER_BIT);
      surface->SwapBuffers();
    }

    std::cout << "presented " << arguments.frames << " frame(s)\n";
    return EXIT_SUCCESS;
  } catch (const refapp::SurfaceError& error) {
    std::cerr << "surface: " << error.what() << "\n";
    return EXIT_FAILURE;
  } catch (const std::exception& error) {
    std::cerr << "fatal: " << error.what() << "\n";
    return EXIT_FAILURE;
  }
}
