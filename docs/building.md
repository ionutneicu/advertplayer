### Building

The application embeds CPython in a C++ host, so it needs the *development*
package of Python (headers plus `libpython`), not just the interpreter.

#### 1. Prerequisites

| Requirement | Minimum | Debian / Ubuntu package |
| ----------- | ------- | ----------------------- |
| C++ compiler with C++20 support | GCC 10 / Clang 12 | `build-essential` |
| CMake | 3.20 | `cmake` |
| CPython headers, `libpython` and interpreter | 3.8 | `python3-dev` |

This section covers building **on the host**. To build in a container instead,
none of the above is needed — only `docker.io`; see
[§5 Building in Docker](#5-building-in-docker).

```bash
sudo apt install build-essential cmake python3-dev
```

Notes:

`CMakeLists.txt` asks for `COMPONENTS Interpreter Development.Embed`, so
**both** a `python3` binary and the development files must be present:

- `python3-dev` supplies both — it pulls `libpython3-dev` for the headers and
  `libpython`, and `python3` for the interpreter. Prefer it.
- `libpython3-dev` alone supplies only the development files. On a desktop
  Ubuntu that usually still works because `python3` is already installed, but
  in a minimal container it fails configure with
  `missing: Python3_EXECUTABLE Interpreter` — while confusingly reporting that
  it *found* a suitable version, because `Development.Embed` was satisfied.
- The plain `python3` package is the mirror image: interpreter, no `Python.h`.
- The package name `libpython3.1-dev` does not exist on current Ubuntu
  releases. To pin a specific minor release use `python3.<minor>-dev`, e.g.
  `python3.14-dev`.

Verify all four pieces before configuring:

```bash
cmake --version && g++ --version && python3 --version && ls /usr/include/python3*/Python.h
```

#### 2. Configure

The build is out-of-tree; `build/` is git-ignored.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
```

A successful configure reports the Python it settled on:

```
-- Found Python3: /usr/bin/python3 (found suitable version "3.13.5", minimum required is "3.8")
      found components: Interpreter Development.Embed
-- Embedding Python 3.13.5 from /usr/lib/x86_64-linux-gnu/libpython3.13.so
```

Both components must appear. `Development.Embed` alone means no `python3`
binary — see [§11](#11-troubleshooting).

Useful configure-time options:

| Option | Effect |
| ------ | ------ |
| `-DCMAKE_BUILD_TYPE=Debug` | Unoptimised, full debug info |
| `-DCMAKE_BUILD_TYPE=RelWithDebInfo` | Default when unset |
| `-DCMAKE_BUILD_TYPE=Release` | Optimised, no debug info |
| `-DPython3_EXECUTABLE=/usr/bin/python3.14` | Build against a specific Python |
| `-DPython3_ROOT_DIR=/opt/python3.14` | Build against a Python outside the system prefix |
| `-GNinja` | Use Ninja instead of Make |

Python discovery is driven by `find_package(Python3 3.8 REQUIRED COMPONENTS
Interpreter Development.Embed)`. `Development.Embed` is deliberate: the plain
`Development` component is satisfied by a headers-only install that cannot
link an embedding host.

#### 3. Build

```bash
cmake --build build -j"$(nproc)"
```

Individual targets:

```bash
cmake --build build --target python_bridge
```

| Target | Kind | Contents |
| ------ | ---- | -------- |
| `python_bridge` | static library | The reusable C++ facade over the CPython C API |
| `python_host` | executable | Demo application driving the facade |

Re-configuring is automatic — editing `CMakeLists.txt` triggers it on the next
build. To start from scratch:

```bash
rm -rf build && cmake -S . -B build && cmake --build build -j"$(nproc)"
```

#### 4. Run

```bash
./build/python_host
```

```bash
ctest --test-dir build --output-on-failure
```

`ctest` runs the `python_host_smoke` test, which executes the demo end to end
and fails if the interpreter cannot be initialised or a script cannot be
imported.

#### 5. Building in Docker

Docker is the supported way to build without installing a toolchain on the
host. Nothing but Docker itself is required.

##### 5.1 Host prerequisites

Only Docker goes on the host — no compiler, no CMake, no Python headers. Those
live in the image.

```bash
sudo apt install docker.io
```

Optionally the buildx component. This project's Dockerfile does **not** need
it; installing it only silences the legacy-builder deprecation notice and
switches to the newer build output:

```bash
sudo apt install docker-buildx
```

Check the daemon is up before going further:

```bash
sudo docker info
```

`docker.io` enables and starts the service on install. If the command reports
it cannot connect to the daemon:

```bash
sudo systemctl enable --now docker
```

**On `sudo`:** every example in this section uses it, because a stock install
requires root for the daemon socket. Adding your user to the `docker` group
removes the need, at the cost of giving that user root-equivalent access to
the host — your call. Either way `$(id -u)` is expanded by *your* shell before
`sudo` runs, so it resolves to your own UID and the file-ownership handling in
[5.5](#55-compile-test-and-run) works unchanged.

##### 5.2 Quick start

From the repository root, three commands — build the image once, then
configure and compile:

```bash
sudo docker build -t opengl-refapp-build docker/
```

```bash
sudo docker run --rm -v "$PWD:/workspace" -u "$(id -u):$(id -g)" opengl-refapp-build cmake -S . -B build-docker -DCMAKE_BUILD_TYPE=RelWithDebInfo
```

```bash
sudo docker run --rm -v "$PWD:/workspace" -u "$(id -u):$(id -g)" opengl-refapp-build cmake --build build-docker -j"$(nproc)"
```

The binary lands on the host at `build-docker/python_host`, but must be run
inside the container — see [5.7](#57-rules-of-the-mounted-build).

The rest of this section explains each part.

##### 5.3 How it is put together

| Path                     | Role                                             |
| ------------------------ | ------------------------------------------------ |
| `docker/Dockerfile`      | `debian:trixie-slim` plus the provisioning step   |
| `docker/install-deps.sh` | The package installation, run during image build |

The image carries **no project source**. It is a toolchain and nothing else:
`build-essential`, `cmake`, `python3-dev`, plus EGL, GLES2 and the surface
backend headers. The working tree is bind mounted
at run time instead of being copied in, which means editing code never
invalidates the image — you build it once and forget about it.

Package installation lives in `install-deps.sh` rather than in `RUN`
directives, so the dependency list can be read, reviewed and shellchecked on
its own:

```dockerfile
COPY install-deps.sh /tmp/install-deps.sh
RUN sh /tmp/install-deps.sh && rm -f /tmp/install-deps.sh
```

The script ends by asserting each thing `CMakeLists.txt` requires — `cmake
--version`, `g++ --version`, `python3 --version`, and `Python.h` at the
interpreter's own `sysconfig` include path — so a broken package set fails
during `docker build` rather than during your first compile.

> **Why not `RUN --mount=type=bind`?** That BuildKit form runs the script
> without ever materialising it in a layer and is the nicer construct, but it
> requires the buildx component, and current Docker has no daemon-integrated
> BuildKit fallback — without buildx the image cannot be built at all. `COPY`
> works on the legacy builder and under BuildKit alike. The script is deleted
> in the same step and is ~1 kB, so the residue in the `COPY` layer is
> immaterial.

##### 5.4 Build the image

The build context is `docker/`, so only the script is sent to the daemon — not
the source tree:

```bash
sudo docker build -t opengl-refapp-build docker/
```

Expect roughly **1.1 GB** and a minute or so on first run, almost all of it
`apt`. Dropping a surface backend or the Python plugin dependencies (below)
trims it.

The legacy builder prints a deprecation notice on the way through. It is
harmless; see [5.1](#51-host-prerequisites) if you want it gone.

**Surface backends.** By default the image carries development packages for
both X11 and Wayland, mirroring the `ENABLE_X11` / `ENABLE_WAYLAND` CMake
options. For a smaller single-backend image, drop one:

```bash
sudo docker build --build-arg WITH_WAYLAND=0 -t opengl-refapp-build docker/
```

Setting both to `0` is refused — there would be no surface backend to build.

**Regenerating the image.** Nothing detects a changed toolchain for you — after
editing `docker/install-deps.sh` or `docker/Dockerfile`, rebuild with the same
command. Editing the script invalidates its `COPY` layer, so `apt` re-runs:

```bash
sudo docker build -t opengl-refapp-build docker/
```

If a stale layer is suspected — an `apt` index cached from a previous build,
say — force a clean rebuild:

```bash
sudo docker build --no-cache -t opengl-refapp-build docker/
```

Existing build trees are configured against the *old* image and will keep
using its CMake cache. Delete them after regenerating:

```bash
rm -rf build-docker
```

If your IDE drives the container it has its own build directory
(`cmake-build-debug`, `cmake-build-*-docker`); delete that one instead, and
have the IDE reload the CMake project so it re-runs configure.

##### 5.5 Compile, test and run

All from the repository root. Each command mounts the working tree at
`/workspace` and runs as the calling user, so generated files are not left
owned by `root`.

Configure:

```bash
sudo docker run --rm -v "$PWD:/workspace" -u "$(id -u):$(id -g)" opengl-refapp-build cmake -S . -B build-docker -DCMAKE_BUILD_TYPE=RelWithDebInfo
```

Compile:

```bash
sudo docker run --rm -v "$PWD:/workspace" -u "$(id -u):$(id -g)" opengl-refapp-build cmake --build build-docker -j"$(nproc)"
```

Run the test suite:

```bash
sudo docker run --rm -v "$PWD:/workspace" -u "$(id -u):$(id -g)" opengl-refapp-build ctest --test-dir build-docker --output-on-failure
```

Run the demo:

```bash
sudo docker run --rm -v "$PWD:/workspace" -u "$(id -u):$(id -g)" opengl-refapp-build ./build-docker/python_host
```

Everything the container writes — `build-docker/`, object files, the binary —
appears in the host working tree, because it is the same directory.

##### 5.6 Interactive use

For anything iterative, take a shell in the container and use the ordinary
commands from sections 2–4 with `build-docker` as the build directory:

```bash
sudo docker run --rm -it -v "$PWD:/workspace" -u "$(id -u):$(id -g)" opengl-refapp-build
```

A shell function removes the repetition from one-shot commands:

```bash
refapp-docker() { sudo docker run --rm -it -v "$PWD:/workspace" -u "$(id -u):$(id -g)" opengl-refapp-build "$@"; }
```

Which reduces the above to:

```bash
refapp-docker cmake --build build-docker -j"$(nproc)"
```

##### 5.7 Rules of the mounted build

- **Use `build-docker/`, never `build/`.** CMake caches absolute paths and the
  detected compiler in `CMakeCache.txt`. A tree configured on the host is not
  reusable inside the container, and vice versa; sharing one directory
  produces confusing configure errors. Both are git-ignored.
- **The binary is not host-portable.** `python_host` links against the
  container's `libpython`, and its baked-in `REFAPP_SCRIPTS_DIR` points at
  `/workspace/scripts`. Run it inside the container; on the host it fails at
  load time unless the host happens to have a matching Python.
- **Always pass `-u`.** Without it the container runs as root and every
  generated file is root-owned on the host. Recovery:
  `sudo chown -R "$(id -u):$(id -g)" build-docker`.
- **`-u` gives an unnamed UID**, so the interactive prompt reads
  `I have no name!`. Cosmetic only.
- **`--rm` is deliberate.** These are throwaway containers; all state that
  matters lives in the mounted working tree.

#### 5a. Raspberry Pi — DispmanX

The DispmanX backend (FR-126) **cannot be built in the container**. It needs
the Broadcom userland — `bcm_host.h` and `libbcm_host` — which exists only on
a Raspberry Pi running the legacy graphics stack (FR-127). Build on the device.

> **Unverified.** These instructions are written from the source and from the
> prior implementation's autoconf build. They have not yet been run on
> hardware — that is story S-02, and this section is updated with what actually
> happens.

##### Prerequisites on the Pi

The legacy graphics driver must be active. On Raspberry Pi OS this is the
non-KMS driver; DispmanX was removed from the default stack at Bullseye, so
which release and driver you use is part of what S-02 records (OP-22).

```bash
sudo apt install build-essential cmake libegl-dev libgles-dev
```

Confirm the Broadcom userland is present before configuring:

```bash
ls /opt/vc/include/bcm_host.h /opt/vc/lib/libbcm_host.so
```

Newer packaged layouts install these under the normal prefixes rather than
`/opt/vc`; the build searches both.

##### Configure and build

```bash
cmake -S . -B build-pi -DENABLE_DISPMANX=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo
```

A successful configure names the library it found:

```
-- DispmanX backend: /opt/vc/lib/libbcm_host.so
```

```bash
cmake --build build-pi -j"$(nproc)"
```

If the Broadcom userland is missing, configure **fails immediately** rather
than producing undefined references at link time:

```
CMake Error: ENABLE_DISPMANX=ON but the Broadcom userland was not found.
    bcm_host.h : BCM_HOST_INCLUDE_DIR-NOTFOUND
    libbcm_host: BCM_HOST_LIBRARY-NOTFOUND
```

##### Run

DispmanX composites a fullscreen layer and needs no display server, so run it
from a console with no X or Wayland session:

```bash
./build-pi/refapp --frames 300
```

Expect a solid blue screen. The program prints the backend it chose, the
surface size and the GL strings:

```
compiled backends: dispmanx
backend    : dispmanx
surface    : 1920x1080
GL_VERSION : OpenGL ES 2.0
```

Backend selection is automatic (FR-7) — DispmanX is chosen when neither
`WAYLAND_DISPLAY` nor `DISPLAY` is set, which is exactly the console case. To
force it:

```bash
./build-pi/refapp --backend dispmanx --frames 300
```

#### 6. Python module resolution

`PythonInterpreter` prepends its `module_search_paths` to `sys.path`, so
imports never depend on the current working directory. `src/main.cc` resolves
that directory in this order:

1. the `REFAPP_SCRIPTS_DIR` environment variable, if set and non-empty;
2. the `REFAPP_SCRIPTS_DIR` compile definition, baked in by CMake as the
   absolute path of `scripts/` in the source tree;
3. the literal `scripts`, relative to the working directory.

Point the binary at a different working copy without rebuilding:

```bash
REFAPP_SCRIPTS_DIR=/path/to/scripts ./build/python_host
```

#### 7. Layout

| Path                        | Contents                                        |
| --------------------------- | ----------------------------------------------- |
| `CMakeLists.txt`            | Build definition, Python discovery               |
| `src/python_interpreter.h`  | C++ facade over the CPython C API                |
| `src/python_interpreter.cc` | Implementation; the only file including Python.h |
| `src/main.cc`               | Demo host driving the facade                     |
| `scripts/`                  | Python modules shipped with the application      |
| `docker/Dockerfile`         | Debian-minimal build image                       |
| `docker/install-deps.sh`    | Toolchain installation for that image            |
| `build/`                    | Host build tree; generated, git-ignored          |
| `build-docker/`             | Container build tree; generated, git-ignored     |

`<Python.h>` is confined to `src/python_interpreter.cc`. The header forward
declares `struct _object` (what `PyObject` is a typedef for) so that host code
links against the facade without inheriting Python's feature-test macros. Keep
it that way: new code should include `python_interpreter.h`, not `<Python.h>`.

#### 8. Adding sources

Add the `.cc` and its `.h` to the `add_library(python_bridge STATIC ...)` list
in `CMakeLists.txt`. Listing headers alongside sources is intentional — it
makes them visible in IDE project trees. Anything new that touches the CPython
C API belongs behind the facade, not in `src/main.cc`.

#### 9. Tooling

`CMAKE_EXPORT_COMPILE_COMMANDS` is on, so `build/compile_commands.json` is
generated for clangd, clang-tidy and similar. Most tools expect it at the
repository root:

```bash
ln -sf build/compile_commands.json compile_commands.json
```

#### 10. Threading

`PythonInterpreter` initialises the runtime on the constructing thread, which
then holds the GIL for the object's lifetime. At most one instance may exist
per process; the constructor throws if an interpreter is already running.
Calling into it from another thread requires acquiring the GIL first
(`PyGILState_Ensure`) — the facade does not do this for you.

#### 11. Troubleshooting

| Symptom | Cause and fix |
| ------- | ------------- |
| `Could NOT find Python3 (missing: Development.Embed)` | `libpython3-dev` is not installed. |
| `Could NOT find Python3 (missing: Python3_EXECUTABLE Interpreter)`, despite `found suitable version` | Development files present, no `python3` binary. Install `python3-dev`, not `libpython3-dev` alone. In a container, fix `docker/install-deps.sh` and regenerate the image ([5.4](#54-build-the-image)). |
| `Could NOT find Python3 (missing: Python3_INCLUDE_DIRS)` | Headers missing for the Python CMake picked. Install the matching `python3.<minor>-dev`, or select another with `-DPython3_EXECUTABLE`. |
| `fatal error: Python.h: No such file or directory` | Configure was cached before the dev package was installed. Delete `build/` and re-configure. |
| `undefined reference to 'Py_InitializeFromConfig'` | Linking against a Python older than 3.8. Check the version printed at configure time. |
| `ModuleNotFoundError: No module named 'refapp_demo'` | `scripts/` was not found. Run with `REFAPP_SCRIPTS_DIR` set to its absolute path. |
| `fatal: a Python interpreter is already running in this process` | A second `PythonInterpreter` was constructed. Only one may be alive at a time. |
| `docker: command not found` | Docker is not installed: `sudo apt install docker.io`. |
| `Cannot connect to the Docker daemon` | The service is not running (`sudo systemctl enable --now docker`), or the command was issued without `sudo` by a user outside the `docker` group. |
| `the --mount option requires BuildKit`, or `BuildKit is enabled but the buildx component is missing` | Only affects Dockerfiles using `RUN --mount`. This one does not; make sure you are on the current `docker/Dockerfile`. |
| `install-deps.sh: not found` during image build | `docker build` was run with the wrong context. The context must be `docker/`. |
| CMake reports a different compiler than expected | `build/` and `build-docker/` were shared. Delete the stale tree; host and container need separate build directories. |
| `error while loading shared libraries: libpython3...` | A container-built binary was run on the host. Run it inside the container. |
| Generated files owned by `root` | `docker run` was issued without `-u "$(id -u):$(id -g)"`. Fix with `sudo chown -R "$(id -u):$(id -g)" build-docker`. |
