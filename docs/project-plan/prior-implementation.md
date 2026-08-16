### Prior implementation

An earlier implementation exists at
[github.com/ionutneicu/opengl-refapp](https://github.com/ionutneicu/opengl-refapp),
GPL-3.0, entirely C (12 `.c`, 9 `.h`).

| Branch | Date | Contents |
| ------ | ---- | -------- |
| `master` | 2017 | Working EGL + GLES2 app, X11 and DispmanX backends, BMP texture, autoconf |
| `cairo-sample-app` | 2019 | Restructured into `app/` + `platform-lib/` + `plugins/`; the plugin API lives here |
| `feature/cmake-support` | Jun 2025 | Partial autoconf → CMake port, `src/libopengl-refapp/` with `backends/x11` and `backends/dispmanx` |

##### What carries over

- **EGL + GLES2 bring-up**, for **both** surfaces:
  `platform-lib/platform-egl-context-x11.c` and
  `platform-lib/platform-egl-context-dispmanx.c`, over the shared
  `opengl-context.c`. Real, working code that proves FR-5 and FR-10 on both
  targets. The most reusable asset in the repository, and the reason
  Raspberry Pi is a `[PoC]` target (FR-126) rather than a later phase — the
  hard part is already written and known to run.
- **The `feature/cmake-support` directory layout** — a platform library with
  per-backend subdirectories — is close to what FR-6 needs.
- It is **C**; this application is C++20, so reuse means porting or wrapping,
  not copying.

##### What is superseded

- **The plugin model is inverted, deliberately.** The old API hands the plugin
  an `OpenGLContext*` and the plugin calls `opengl_load_texture_in_gpu()` and
  `opengl_draw_texture()` itself — *plugins drove GL*. This design has the
  plugin describe draw items and residency operations while the application
  renders. That inversion is what buys FR-32 language independence and keeps
  FR-3 and FR-21 reachable at all.

  > Do not reuse `plugins/plugin.h`. Its shape is incompatible with every
  > transport requirement in this document.

- **Per-frame drawing helpers** such as `opengl_draw_texture(ctx, texture,
  scale)` assume axis-aligned quads and cannot express FR-63 rotation.

##### What was never finished

Stated so nobody assumes otherwise:

- **Dynamic loading does not exist.** The directory-scanning path in
  `app/opengl-plugin-registry.c` is inside `#if 0`, and
  `try_load_plugin()` is a stub returning 0. The live code is a hardcoded
  `strcmp(plugin_name, "static.picture")`.
- **There is no Cairo plugin**, despite the branch name.
  `scrolling-text-plugin.c` is 408 bytes: one struct declaration, no
  functions, no Cairo. Cairo appears only in `configure.ac` and a file-header
  comment.
- `static-picture.c` is the only working plugin — a hand-rolled BMP parser.
- The CMake branch calls `add_subdirectory(src/plugin_app)` on a directory
  that is not in the tree, so it does not configure as-is.

##### What it says about open points

The prior code is evidence, not precedent — but it is the author's own earlier
judgement and worth weighing.

| Bears on | Prior implementation did | This document proposes |
| -------- | ------------------------ | ---------------------- |
| **OP-15** plugin discovery | Scanned the directory for `*.so` (in the disabled path) | Filename convention, `plugins/<type>.so` (FR-25) |
| **OP-8** argument typing | Typed union: `int` / `float` / `string`, looked up by path | Strings across the ABI, typed accessors in `libplugin_dev` |
| **FR-62** config format | Registry comment already says "json-formatted string containing plugin's initializer parameters" | JSON — agrees |
| **FR-88** helper library | `opengl-plugin-params-helper.h`: *"Naive implementation... this object is critical and deserves complete library for doing this"* | `libplugin_dev` — agrees |
| **FR-99 – FR-103** reflection | `plugin_property_descriptor_t` with `PT_STRING` / `PT_INT` and a hints byte, marked *"Very Limited descriptior"* | Typed, bounded parameter schema — the fuller version |

The first two rows are the ones where this document departs from prior
practice. Both are recorded as open points and both are cheap to reverse
before implementation starts.
