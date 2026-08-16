cmake_minimum_required(VERSION 3.20)

project(opengl-refapp
        VERSION 0.1.0
        DESCRIPTION "Reference application embedding CPython in a C++ host"
        LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

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

enable_testing()
add_test(NAME python_host_smoke COMMAND python_host)
set_tests_properties(python_host_smoke PROPERTIES
  PASS_REGULAR_EXPRESSION
    "frame width : 2560|Done\\.|caught PythonError:")
