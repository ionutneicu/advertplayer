cmake_minimum_required(VERSION 3.20)

project(opengl-refapp
        VERSION 0.1.0
        DESCRIPTION "Plugin-driven 2D OpenGL ES renderer"
        LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

# ---------------------------------------------------------------------------
# Surface backends (FR-6). Each is a separate implementation of one interface,
# never a fork of the renderer above it (risk R-4).
#
# ENABLE_DISPMANX defaults OFF because it cannot build without the Broadcom
# headers and libbcm_host, which exist only on a Raspberry Pi running the
# legacy graphics stack (FR-127). The x86-64 build container cannot provide
# them, so a green container build says nothing about this backend.
# ---------------------------------------------------------------------------
option(ENABLE_DISPMANX "Build the DispmanX surface backend (Raspberry Pi)" OFF)

set(REFAPP_SURFACE_SOURCES src/platform/surface.cc src/platform/surface.hpp)

if(ENABLE_DISPMANX)
  # Locate the Broadcom userland. /opt/vc is the legacy layout; the packaged
  # layout puts them under the normal prefixes.
  find_path(BCM_HOST_INCLUDE_DIR
    NAMES bcm_host.h
    HINTS /opt/vc/include /usr/include
    PATH_SUFFIXES interface/vmcs_host/linux interface/vcos/pthreads)
  find_library(BCM_HOST_LIBRARY NAMES bcm_host HINTS /opt/vc/lib /usr/lib)

  # Fail here, with the reason, rather than at link time with a page of
  # undefined references (FR-127).
  if(NOT BCM_HOST_INCLUDE_DIR OR NOT BCM_HOST_LIBRARY)
    message(FATAL_ERROR
      "ENABLE_DISPMANX=ON but the Broadcom userland was not found.\n"
      "  bcm_host.h : ${BCM_HOST_INCLUDE_DIR}\n"
      "  libbcm_host: ${BCM_HOST_LIBRARY}\n"
      "This backend builds only on a Raspberry Pi with the legacy graphics "
      "stack; it cannot be built in the x86-64 container. Build on the device, "
      "or configure with -DENABLE_DISPMANX=OFF. See docs/building.md.")
  endif()

  message(STATUS "DispmanX backend: ${BCM_HOST_LIBRARY}")
  list(APPEND REFAPP_SURFACE_SOURCES
    src/platform/dispmanx/surface_dispmanx.cc
    src/platform/dispmanx/surface_dispmanx.hpp)
endif()

add_library(refapp_platform STATIC ${REFAPP_SURFACE_SOURCES})
target_include_directories(refapp_platform PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_definitions(refapp_platform
  PUBLIC REFAPP_ENABLE_DISPMANX=$<BOOL:${ENABLE_DISPMANX}>)

if(ENABLE_DISPMANX)
  target_include_directories(refapp_platform SYSTEM PUBLIC ${BCM_HOST_INCLUDE_DIR})
  target_link_libraries(refapp_platform PUBLIC ${BCM_HOST_LIBRARY})
endif()

# EGL and GLES2 are needed by every backend (FR-5, FR-10).
#
# The brcm* names come first deliberately. On the Pi legacy stack /opt/vc
# ships both libEGL.so and libbrcmEGL.so with identical content, but a device
# that also has Mesa installed may resolve the generic name to Mesa's
# implementation at load time -- which would hand a Mesa context a DispmanX
# window and fail in a way that looks like a driver bug. The brcm* names are
# unambiguous. On a desktop neither exists, so the search falls through to the
# generic names, which is what we want there.
find_library(EGL_LIBRARY NAMES brcmEGL EGL HINTS /opt/vc/lib)
find_library(GLESV2_LIBRARY NAMES brcmGLESv2 GLESv2 HINTS /opt/vc/lib)
if(EGL_LIBRARY AND GLESV2_LIBRARY)
  target_link_libraries(refapp_platform PUBLIC ${EGL_LIBRARY} ${GLESV2_LIBRARY})
else()
  message(WARNING
    "EGL or GLESv2 not found; refapp will not link. Install libegl-dev and "
    "libgles-dev, or build with the container image.")
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(refapp_platform PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(refapp src/refapp_main.cc)
target_link_libraries(refapp PRIVATE refapp_platform)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(refapp PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ---------------------------------------------------------------------------
# The CPython embedding demo the project started from. Moves under the Python
# plugin boilerplate in story S-07; kept building until then.
#
# Off by default when cross-compiling: the Raspberry Pi cross image has no
# armhf Python, and the demo is not what a cross build is for. The renderer
# itself has no Python dependency.
# ---------------------------------------------------------------------------
if(CMAKE_CROSSCOMPILING)
  set(REFAPP_PYTHON_DEMO_DEFAULT OFF)
else()
  set(REFAPP_PYTHON_DEMO_DEFAULT ON)
endif()
option(BUILD_PYTHON_DEMO "Build the CPython embedding demo (legacy, see S-07)"
       ${REFAPP_PYTHON_DEMO_DEFAULT})

if(BUILD_PYTHON_DEMO)

# "Development.Embed" is the component that ships libpython plus the headers
# required to embed the interpreter in a host process. On Debian/Ubuntu it
# comes from libpython3-dev; the plain "Development" component would also
# accept a headers-only install that cannot link.
find_package(Python3 3.8 REQUIRED COMPONENTS Interpreter Development.Embed)

message(STATUS "Embedding Python ${Python3_VERSION} from ${Python3_LIBRARIES}")

# ---------------------------------------------------------------------------
# python_bridge: the reusable C++ facade over the CPython C API.
# ---------------------------------------------------------------------------
add_library(python_bridge STATIC
  src/python_interpreter.cc
  src/python_interpreter.h)

target_include_directories(python_bridge PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(python_bridge PUBLIC Python3::Python)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(python_bridge PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ---------------------------------------------------------------------------
# python_host: the demo executable that drives the bridge.
# ---------------------------------------------------------------------------
add_executable(python_host src/main.cc)
target_link_libraries(python_host PRIVATE python_bridge)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(python_host PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Baked-in location of the shipped Python modules. Overridable at run time
# through the REFAPP_SCRIPTS_DIR environment variable.
target_compile_definitions(python_host
  PRIVATE "REFAPP_SCRIPTS_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/scripts\"")

endif()  # BUILD_PYTHON_DEMO

enable_testing()
if(BUILD_PYTHON_DEMO)
add_test(NAME python_host_smoke COMMAND python_host)
set_tests_properties(python_host_smoke PROPERTIES
  PASS_REGULAR_EXPRESSION
    "frame width : 2560|Done\\.|caught PythonError:")
endif()
