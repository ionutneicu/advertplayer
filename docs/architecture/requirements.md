### Requirements

Status: **draft**. This document is now the source of record. It supersedes the
original unstructured `requirements.txt`, which has been removed; the
open-question round that shaped it is kept verbatim in
[requirements-q-and-a.md](requirements-q-and-a.md) for traceability.

Identifiers defined here: `FR-n` functional, `NFR-n` non-functional, `DOC-n`
documentation, `OUT-n` out of scope. Open points (`OP-n`) and risks (`R-n`) are
defined in [../project-plan/](../project-plan/). Anything no source states is
raised as an open point rather than silently decided.

This document is *what* the system must do. Sequencing, readiness and
outstanding decisions are in [../project-plan/](../project-plan/).

> The plugin ABI is now specified separately in
> [plugin-api.md](plugin-api.md) and
> [`include/refapp/plugin.h`](../../include/refapp/plugin.h). Its §6 proposes
> revisions to **FR-41 – FR-43** (prefetch merged into uploads) and **OP-8**
> (argument values are strings), and resolves the FR-24 / R-13 conflict over
> who resolves media paths. Those three are pending sign-off.

#### 1. Phase tags

Every requirement carries a phase tag. **The `[PoC]` tag is the contract for
what gets built first** — everything else exists in this document only so the
PoC does not paint itself into a corner.

| Tag | Meaning |
| --- | ------- |
| **`[PoC]`** | In the proof of concept. Must work end to end. |
| **`[P2]`** | Next phase. Not built now, but the PoC must not preclude it. |
| **`[L]`** | Long term / needs research. Direction only. |

Sequencing, milestones and what is currently in scope live in
[../project-plan/roadmap.md](../project-plan/roadmap.md).

#### 2. Purpose and scope

A Linux application that renders 2D content through native OpenGL drivers and
delegates *what* to render to a set of plugins queried once per frame.

The application owns the GL context, the render loop, GPU memory and the scene
sequence. Plugins own the content. Every frame the application asks each plugin
what it wants on screen; the plugin answers with drawing work, residency
changes, and hints about resources it will need later.

#### 2a. System decomposition

The system is four applications, two on each side. Only the **Renderer** is in
the PoC; the rest are described because the PoC's data layout has to be one
they can later act on.

```
CLIENT                                  SERVER
┌──────────────┐                        ┌──────────────┐
│  Renderer    │   never talks to net   │  Web         │
│  [PoC]       │                        │  interface   │
└──────┬───────┘                        │  [P2]        │
       │ reads                          └──────┬───────┘
┌──────▼───────┐                               │ edits
│ Local storage│                        ┌──────▼───────┐
│  FS + DB     │                        │ Server data  │
└──────▲───────┘                        │  FS + DB     │
       │ writes                         └──────▲───────┘
┌──────┴───────┐      replication       ┌──────┴───────┐
│ Updater      │◄──────────────────────►│ Updater      │
│  [P2]        │                        │  [P2]        │
└──────────────┘                        └──────────────┘
```

- **FR-106** `[PoC]` — The **Renderer** works **autonomously** from local
  storage. It never contacts the network and does not depend on the Updater
  running. A client with no connectivity keeps playing what it has.

##### Renderer and Updater talk over IPC

- **FR-128** `[P2]` — The Renderer and the client Updater communicate over
  **Protocol Buffers on a UNIX domain socket**. This is a local control
  channel, not network access: FR-106 stands — the Renderer still reaches no
  server, and still plays whatever is on disk if the Updater is absent.

- **FR-129** `[P2]` — Commands **Updater → Renderer**:

  | Command | Meaning |
  | ------- | ------- |
  | `StorageUpdated` | New data is in place and referentially complete (FR-121). The Renderer re-caches, normally letting the running scene finish first. |
  | `ForceRefresh` | Stop the scene now, reload, restart. The override of FR-119. |
  | `SuspendScreen` | Blank the screen, either for a timeout or on a start/stop schedule table. |

- **FR-130** `[P2]` — Commands **Renderer → Updater**, so the Renderer can read
  a consistent filesystem while it re-caches:

  | Command | Reply |
  | ------- | ----- |
  | `LockFS(timeout)` | `LOCK_ACCEPTED` — data is referentially complete and frozen; `LOCK_NOT_POSSIBLE` — the filesystem is mid-change; `LOCK_EXTENDED` — an existing lock was prolonged |
  | `ReleaseFS` | The Updater may write again |

- **FR-131** `[P2]` — `LockFS` carries a **timeout, bounded at 100 s**, and the
  Updater resumes writing when it expires. A Renderer that dies mid-cache must
  not freeze updates forever.

- **FR-132** `[P2]` — A `SuspendScreen` command **replaces** the previous one
  outright, and may carry no suspend arguments at all, which cancels
  suspension. Replacement is what makes contradictions impossible: there is
  only ever one active suspension policy, so two schedules can never disagree.

> Overlaps *within* one schedule table are a different question and are
> resolved by union — the screen is suspended if any range in the table covers
> the current time.

- **FR-107** `[P2]` — The **client Updater** is a separate application. It
  keeps local storage in sync with the server and is the **only** client
  component that touches the network.
- **FR-108** `[P2]` — The **server Updater** replicates server-side data to
  clients. It communicates with client Updaters and nothing else.
- **FR-133** `[P2]` — The Updater assembles new data in a **staging area of
  its own**, and publishes it to a location the Renderer reads only once it is
  referentially complete (FR-121). The Renderer is never pointed at
  half-written data.

  > The alternative — the Renderer briefly locking the Updater's own working
  > area and snapshotting out of it — was considered and rejected: it copies
  > the whole data set on every update and doubles filesystem usage. `LockFS`
  > (FR-130) remains, but for reading a *published* set consistently, not for
  > snapshotting an in-progress one.

- **FR-134** `[P2]` — The Updater keeps a **checksum per file** and does not
  download content it already holds, asking the server by checksum first —
  the same idea HTTP uses for conditional image fetches.

  > This is content addressing (FR-85) seen from the Updater's side, and it is
  > already what the download diff of FR-69 requires. One consequence: if the
  > checksum *is* the storage identity, it must be collision resistant, so
  > **SHA-256 rather than MD5**. MD5 collisions are cheap to manufacture, and a
  > collision in a content-addressed store means one file silently masquerading
  > as another.

- **FR-109** `[P2]` — The **web interface** edits server-side data: create a
  scene, load plugins and data, assemble the ordered plugin list, edit
  parameters, and upload the images those parameters reference. It is the
  scene editor of FR-104.
- **FR-110** `[PoC]` — Local storage is a **filesystem plus a database**. What
  goes in each is settled below (FR-135 – FR-138).

##### What goes in the database, and what stays in the filesystem

- **FR-135** `[PoC]` — The **database holds plugin arguments** and the
  structured records around them — scenes, plugin lists, parameters. It holds
  no BLOBs.
- **FR-136** `[PoC]` — **Large binary content stays in the filesystem**: images,
  video, long texts, fonts. An argument that refers to one holds a **URI**, not
  the bytes.
- **FR-137** `[PoC]` — Storage URIs use **four** schemes, each expanding under
  the Renderer's storage root. The tier decides both *who shares* an asset and
  *how long it lives*:

  | Scheme | Expands to | For | Lives until |
  | ------ | ---------- | --- | ----------- |
  | `system_storage://` | `<root>/system/` | Cross-scene, cross-plugin: fonts and similar | Removed by administration |
  | `scene_storage://` | `<root>/<scene_id>/shared/` | Shared between plugins of one scene | The scene retires |
  | `plugin_storage://` | `<root>/plugins/<plugin_type>/` | A plugin **type**'s own assets, identical for every instance and **not user-configurable** | The plugin is uninstalled |
  | `instance_storage://` | `<root>/<scene_id>/<instance_id>/` | Assets a **single configured instance** references | The scene retires |

- **FR-192** `[PoC]` — `plugin_storage://` is **per plugin type, not per
  instance**, and holds what the plugin needs but the user never configures.

  > The case that requires it: a parameter is an enum, and the plugin fetches a
  > different image depending on which value was chosen — without the user
  > knowing an image was involved. That asset belongs to the plugin, is
  > identical across every instance, and must survive a scene retiring. It is
  > not per-instance data and it is not scene data.
  >
  > It also gives hardcoded plugin assets — a decoration, a rounded corner —
  > somewhere to live that cannot collide with configured ones. Declared
  > parameters stay the plugin's public surface; `plugin_storage://` is its
  > private one.

- **FR-193** `[PoC]` — Storage tiers and **ABI resource ids are unrelated
  namespaces**. `texture_id` and `shader_id` in `refapp_draw_item` are
  per-plugin-**instance** runtime handles for GPU objects (OP-10), chosen by
  the plugin and meaningful only while it lives. They name nothing on disk.

  ```
  Plugin: Static Image
      background: scene_storage:///backgrounds/bkg_0001.jpg

  Plugin: Text
      text: "Hello"
      font: system_storage://Helvetica_fixed
  ```

- **FR-138** `[PoC]` — URI expansion happens in the **application**, and the
  plugin receives the resolved entry (risk R-13). A plugin never parses a
  scheme or builds a path.

> This resolves OP-18 and refines FR-61 rather than contradicting it. The three
> schemes are a **naming** scheme — they say who an asset belongs to and how
> long it lives. Physical layout stays free: content addressing (FR-85) can
> still store one blob once and let several URIs across several scenes resolve
> to it. The URI is the logical name; the blob is the storage.
>
> `system_storage://` earns its place by having a different **lifetime**: it
> survives scene retirement, so cleanup (FR-76) must not collect a font merely
> because no current scene references it.

> The separation in FR-106 and FR-107 is the load-bearing one. Because
> Renderer only ever reads local storage, updating is invisible to it: the
> Updater's job is to leave local storage in a state the Renderer can pick up,
> which is exactly what the immutability and staging rules of
> [§7a](#7a-content-distribution-and-updates) provide. Nothing in the PoC may
> assume the two are one process.

#### 2b. System properties

Plugins need facts about the machine they are running on that are neither
scene content nor arguments: what time zone it is in, what shape its display
is, what language its audience reads.

- **FR-154** `[PoC]` — The application **queries the system for local
  properties** and passes them to every plugin instance at `create`.
- **FR-155** `[PoC]` — Properties are a **key/value table**, not a fixed
  struct, so a new property costs no ABI change. Values are strings, with typed
  accessors in `libplugin_dev`, consistent with the argument encoding decision
  (plugin-api §8.3).
- **FR-156** `[PoC]` — Well-known keys are **documented in the header** and
  namespaced by origin. A plugin that does not recognise a key ignores it; a
  plugin that needs one that is absent uses its own default.

  | Key | Source | Example |
  | --- | ------ | ------- |
  | `system.timezone` | OS | `Europe/Bucharest` |
  | `system.locale` | OS | `ro_RO.UTF-8` |
  | `system.language` | OS or configuration | `ro` |
  | `display.aspect_class` | Surface layer | `16:9` |
  | `display.orientation` | Surface layer | `LANDSCAPE` |
  | `display.aspect_ratio` | Surface layer | `1.7778` |
  | `gl.max_texture_size` | GL | `2048` |
  | `budget.texture_memory_mb` | Application | Per-instance allowance (FR-205) |
  | `client.id` | Configuration | assigned at provisioning |
  | `install.tilt_angle` | Server-configured | `0` |

- **FR-213** `[PoC]` — Properties fall into **two classes**, and the class is
  part of a key's definition:

  | Class | Origin | Example | Changes when |
  | ----- | ------ | ------- | ------------ |
  | **Observed** | Hardware, OS or driver | `gl.max_texture_size`, `display.colour_depth`, `system.timezone` | The machine changes |
  | **Configured** | Set at installation, **and settable from the server** | `install.tilt_angle`, `client.id` | The server pushes a change |

- **FR-214** `[P2]` — Configured properties are **pushed from the server** the
  way content is: staged, activated at a defined moment (FR-86), never edited
  under a running scene.

  > This is what makes tilt angle expressible. It is not a fact about the
  > hardware — the same panel on the same board can be hung straight or at an
  > angle — and it is not scene content either, because every scene on that
  > screen is affected. It is an *installation* fact, and there was no category
  > for it until now.

- **FR-215** `[PoC]` — Plugins receive both classes **identically**. A plugin
  cannot tell whether a value was measured or configured, and must not care.

- **FR-216** `[PoC]` — `display.aspect_class` is an **extensible
  enumeration** — the same mechanism as plugin-declared enums (FR-182): a named
  set that may gain members at any time, an integer underneath. The **PoC
  defines `16:9` and `OTHER` only**.

  > 16:9 covers the overwhelming majority of panels, and the fallback rule
  > below means `4:3` or `21:9` can be added later without touching anything:
  > plugins built against the smaller set already had to handle a value they
  > did not recognise.

- **FR-217** `[PoC]` — A plugin must behave correctly on `OTHER`, or on any
  `aspect_class` value it does not recognise, by falling back to
  `display.aspect_ratio`. Layout by class, geometry by ratio.

- **FR-160** `[PoC]` — Display resolution is **not** published to plugins.
  Colour depth and refresh rate are, because they change what a plugin
  *decides*, not what size it draws:

  | Key | Example | Used for |
  | --- | ------- | -------- |
  | `display.colour_depth` | `24` | Whether to dither; banding on gradients |
  | `display.refresh_hz` | `60` | Sanity-checking the frame budget (NFR-1) |

- **FR-161** `[PoC]` — **GL capabilities**, queried from the driver:

  | Key | Used for |
  | --- | -------- |
  | `gl.max_texture_size` | See FR-157 |
  | `gl.compressed_formats` | Which compressed formats exist here, for OUT-1 when it lands |
  | `gl.memory_mb` | GPU memory, where the driver exposes it — the ceiling NFR-4 needs |
  | `gl.extensions` | Capability checks such as `GL_EXT_texture_format_BGRA8888` (FR-93) |

- **FR-162** `[PoC]` — **Host capabilities**: CPU core count, user-space memory
  available, free storage. A plugin uses these to decide how much work to
  attempt — how far ahead a slideshow prefetches, whether an expensive Cairo
  effect is affordable — and the application uses them for its own budgeting.

  > These are the honest reason the Raspberry Pi target exists. A plugin that
  > behaves identically on a desktop and a Pi is either wasting the desktop or
  > overwhelming the Pi.

- **FR-157** `[PoC]` — `gl.max_texture_size` matters because a plugin
  producing a texture larger than the GPU accepts fails at upload. A Raspberry
  Pi's limit is far below a desktop's, and a plugin that generates a
  full-width marquee needs to know before it draws.

##### Server-side asset preparation

- **FR-170** `[P2]` — Assets are prepared **server-side, sized for the client
  that will show them**. The Renderer displays what it is given; it does not
  scale to fit a resolution it was never told.
- **FR-171** `[P2]` — The **Updater** — not the Renderer — tells the server
  what the client needs: its resolution, rotation, colour depth and the rest of
  the capability report (FR-150). The Renderer never participates in that
  conversation and never learns the answer.
- **FR-172** `[P2]` — The Renderer therefore stays **display-agnostic**. It may
  well be showing a texture cut for exactly its panel, and it cannot tell.

> This is the cleaner division. Resolution-dependent work — rescaling images,
> rasterising text at the right size — happens once on the server, for many
> clients, rather than on every device at every scene start. It also keeps the
> capability report in one place: the Updater already talks to the server, and
> the Renderer already talks to nothing.
>
> **It does not cover procedurally-drawn content.** The analog clock and the
> marquee (FR-114) rasterise with Cairo on the client, at run time, and must
> choose some pixel size without being told the resolution. See
> [OP-33](../project-plan/open-decisions.md#outstanding--proposed-defaults).

##### Rotation

- **FR-163** `[PoC]` — Display rotation is applied by the **application**, in
  the projection it already owns. Plugins are unaware of it and need no code
  for it.
- **FR-164** `[PoC]` — The aspect properties a plugin receives describe the
  **post-rotation** display. A 16:9 panel mounted at 90° presents as `16:9`
  class, `PORTRAIT` orientation, ratio `0.5625`. The plugin lays out for what
  it will actually occupy.
- **FR-165** `[PoC]` — Rotation of **0°, 90°, 180° and 270°** is supported.
  These are exact: the rotated output still fills the panel.
- **FR-173** `[PoC]` — Rotation is a **per-screen mounting parameter**,
  configured on the client. It describes how the panel is bolted to the wall,
  not anything about the content.
- **FR-174** `[P2]` — The mounting parameter is **reported to the server** with
  the rest of the capability report (FR-171), so the server can size assets for
  what the screen effectively is.
- **FR-175** `[P2]` — The server derives **effective display geometry** from
  panel geometry plus mounting rotation, and prepares assets for that. A
  1920×1080 panel mounted at 90° is a 1080×1920 display as far as content is
  concerned.
- **FR-176** `[P2]` — The **web preview shows the effective geometry** — the
  portrait canvas, in that example — because that is what a viewer standing in
  front of the screen sees.

  > Which means **the preview does not implement rotation at all**. Rotation
  > compensates for how the panel is mounted; it cancels out from the viewer's
  > point of view. The preview needs the *effective geometry* (FR-175), not the
  > angle. That removes the risk of two implementations of the same transform —
  > one in GL, one in the browser — disagreeing about a corner case.
  >
  > It also means the preview is per **profile**, not per device: two clients
  > with the same effective geometry preview identically however their panels
  > are hung.

- **FR-166** `[L]` — **Arbitrary angles** are not supported.

  > A tilted mount is a real installation, but it is a different feature, not a
  > larger number in the same field. At any angle that is not a multiple of 90°
  > the logical rectangle no longer maps onto the physical panel: the content
  > must be cropped, letterboxed into a bounding box, or drawn oversized and
  > clipped — and something has to decide which. That is a design decision per
  > scene, not a display property. See
  > [OP-31](../project-plan/open-decisions.md#outstanding--proposed-defaults).

- **FR-158** `[P2]` — Properties that can change while a scene runs — time zone
  across a DST boundary, a resized desktop window — cause the **scene to
  restart**, as FR-149 already specifies for display geometry. No per-frame
  property delivery.

#### 3. Core concepts

| Term | Meaning |
| ---- | ------- |
| **Scene** | The unit of playback. One scene is displayed at a time; it declares an ordered list of plugin instances and an end condition. |
| **Plugin** | A shared object exposing a C API. Instantiated and initialised at scene start, queried once per frame. |
| **Plugin instance** | One configured use of a plugin within a scene: a name, a type, and arguments. |
| **Frame query** | The per-frame call to a plugin instance, carrying the scene-relative timestamp. |
| **NDC** (normalised device coordinates) | OpenGL's screen-independent coordinate space: `-1` to `+1` on each axis, origin at the centre, regardless of pixel count. What "screen agnostic coordinates" means concretely. |
| **Draw item** | One request to display a portion of a texture at a location on screen, with a given shader. |
| **TTS** (time to screen) | A plugin's hint of when a not-yet-loaded texture will be needed, so the application can upload it in advance. |
| **Identity shader** | Default pass-through shader created by the application; samples a texture and writes it unmodified. |

#### 4. Target platforms

| ID | Phase | Platform | Surface | Driver |
| -- | ----- | -------- | ------- | ------ |
| FR-1 | `[PoC]` | Native Linux desktop, X11 | EGL over X11 | Mesa or vendor |
| FR-2 | `[PoC]` | Native Linux desktop, Wayland | EGL over Wayland | Mesa or vendor |
| FR-126 | `[PoC]` | Raspberry Pi | EGL over **DispmanX** | Broadcom |
| FR-3 | `[L]` | Browser | WebGL | — |
| FR-115 | `[L]` | Android client | — | — |

Three surface backends, all reaching GL through EGL (FR-5), so the renderer
above them is identical on all three.

> **DispmanX constrains which Raspberry Pi OS.** It is the legacy Broadcom
> display API, provided by `libbcm_host` from `/opt/vc`. It was removed from
> the default graphics stack in Raspberry Pi OS Bullseye, which moved to
> KMS/DRM with Mesa v3d. Targeting DispmanX therefore means Buster or earlier,
> or a later release configured with the legacy driver. Which of those is
> intended is [OP-22](../project-plan/open-decisions.md#outstanding--proposed-defaults).
>
> This also **reinforces FR-10**: the legacy Broadcom stack is GLES2-only, so
> GLES3 was never available on this target anyway.
> **Whether DispmanX is worth its cost is reopened as
> [OP-24](../project-plan/open-decisions.md#outstanding--proposed-defaults).**
> The goal behind it — proving the system runs on constrained hardware — does
> not require the *legacy* stack, and a current Raspberry Pi on KMS/DRM proves
> the same thing at a fraction of the cost.

- **FR-4** `[PoC]` — Must work with native OpenGL drivers; must not depend on a
  software rasteriser.
- **FR-5** `[PoC]` — The surface layer goes through **EGL**, never GLX. This is
  what lets one GL path serve X11, Wayland and DispmanX alike, and keeps FR-3
  reachable. DispmanX is an EGL platform too, so it costs a surface backend
  rather than a second renderer.
- **FR-6** `[PoC]` — Surface backends are selectable at build time through
  CMake options:

  | Option | Default | Effect |
  | ------ | ------- | ------ |
  | `ENABLE_X11` | `ON` | Build the X11 surface backend |
  | `ENABLE_WAYLAND` | `ON` | Build the Wayland surface backend |
  | `ENABLE_DISPMANX` | `OFF` | Build the DispmanX surface backend |

  `ENABLE_DISPMANX` defaults **off** because it cannot build without the
  Broadcom headers and `libbcm_host`, which exist only on Raspberry Pi — see
  FR-127. Configuring with all three `OFF` is a configure-time error.

- **FR-7** `[PoC]` — When several backends are built, the active one is chosen
  at run time, with a command-line override:

  | Order | Condition | Backend |
  | ----- | --------- | ------- |
  | 1 | `WAYLAND_DISPLAY` set | Wayland |
  | 2 | `DISPLAY` set | X11 |
  | 3 | Neither, and DispmanX was built | DispmanX |

  The fallback works because DispmanX has no window system at all: it composites
  a fullscreen layer directly, which is exactly the case where no display-server
  variable is set.

- **FR-127** `[PoC]` — The DispmanX backend needs Broadcom headers and
  `libbcm_host`, which the x86-64 build container cannot supply. Building it
  requires **on-device compilation or a Raspberry Pi sysroot**; the build must
  detect their absence and fail at configure time with a clear message rather
  than at link time. How that build is provisioned is
  [OP-23](../project-plan/open-decisions.md#outstanding--proposed-defaults).
- **FR-8** `[PoC]` — Platform and surface selection must be invisible to plugin
  code.
- **FR-9** `[PoC]` — The container toolchain (`docker/install-deps.sh`) takes
  matching flags so an image can be built with only the needed graphics
  development packages. It provisions X11 and Wayland only: DispmanX is not
  packaged for x86-64 Debian (FR-127).
> **Which hardware and drivers are tested on is
> [OP-25](../project-plan/open-decisions.md#outstanding--proposed-defaults)**,
> with the resulting matrix in [test-plan.md](../test-plan.md). Building in
> Docker is settled; testing is not, because a container has no GPU.

- **DOC-1** `[PoC]` — Document how to run against **vendor GL/GLES libraries**
  rather than the generic Mesa path: NVIDIA's proprietary stack, GPU selection
  on hybrid systems (`DRI_PRIME`, `__GLX_VENDOR_LIBRARY_NAME`,
  `__EGL_VENDOR_LIBRARY_FILENAMES`, `MESA_LOADER_DRIVER_OVERRIDE`), and how to
  verify which driver is actually in use (`eglinfo`, `es2_info`).

  > For AMD and Intel there is normally nothing to switch to — Mesa's
  > `radeonsi` and `iris` *are* the native drivers. The cases that matter are
  > NVIDIA and multi-GPU selection.

#### 5. Rendering model

- **FR-10** `[PoC]` — **OpenGL ES 2.0.** Decided; GLES3 is not targeted.
- **FR-11** `[PoC]` — 2D only: clip a region of a texture and display it on
  screen.
- **FR-12** `[PoC]` — Geometry uses `GL_TRIANGLES` only.
- **FR-13** `[PoC]` — Draw-item coordinates at the application boundary are
  OpenGL normalised device coordinates, `[-1, 1]`. **A plugin is not told the
  screen resolution and must not need it.**

  > Reaffirmed after being briefly relaxed. Resolution-dependent rasterisation
  > belongs on the server, not in the client — see FR-170. The client stays
  > agnostic; assets arrive already sized for it.

- **FR-14** `[PoC]` — Plugin output composites in **scene plugin-list order**:
  the first entry is bottom-most, each subsequent entry paints over those
  before it.
- **FR-15** `[PoC]` — Alpha blending is enabled; each plugin's output blends
  with what is already on screen via the texture's alpha channel.

#### 6. Scene model

- **FR-16** `[PoC]` — One scene is displayed at a time.
- **FR-17** `[PoC]` — A scene's end condition is declared in its definition and
  is one of:

  | Condition | Meaning |
  | --------- | ------- |
  | `duration` | Ends after a fixed time. |
  | `all` | Ends when **all** plugin instances report termination. |
  | `any` | Ends when **any** plugin instance reports termination. |
  | `never` | Runs until replaced from outside (schedule change, config update). |

- **FR-18** `[PoC]` — All of a scene's plugin instances are created and
  initialised at scene start via `Init()`, receiving their configured
  arguments.
- **FR-19** `[PoC]` — A plugin instance need not be visible in every frame. It
  may return an empty display set while still requesting textures and shaders
  be prepared, so it is ready when it does become visible.
- **FR-20** `[PoC]` — Plugin instances are destroyed at scene end and
  re-created at scene start, so every scene start is a clean slate.

#### 7. Scene configuration

##### Data model

Three levels, each owning the one below:

```
Scene list                       ordered; what plays, and in what order
 └── Scene                       name + end condition + ordered plugin list
      └── Plugin instance        instance name + type + arguments
           └── (references) ──►  Media storage: one flat shared root
```

| Level | Owns | Notes |
| ----- | ---- | ----- |
| **Scene list** | The set of scenes and their play order | One file. Replaced by the schedule (FR-28) in `[P2]`. |
| **Scene** | End condition, ordered plugin instance list | One file per scene, found by name. Plugin **order is declared here**. |
| **Plugin instance** | Instance name, plugin type, arguments | Arguments are per instance, so the *same* plugin type takes different arguments in different scenes, or twice in one scene. |
| **Media storage** | Images and any other media | **One flat root, shared across all scenes and plugins** — media is commonly reused, so it is stored once. Argument values are paths relative to it. |

- **FR-60** `[PoC]` — A **scene list** declares which scenes exist and the order
  the application plays them in. It is the PoC's driver (FR-29) and the thing
  the `[P2]` schedule (FR-28) later replaces.
- **FR-61** `[PoC]` — Media storage is a **single flat root, shared between
  scenes and plugins**, not partitioned per scene or per plugin. The same image
  referenced by two scenes is stored once. Argument values are paths relative
  to the root and may contain subdirectories.

Directory layout:

```
scenes/
    scenes.json           the scene list
    midnight.json         one file per scene, found by name
    morning.json
plugins/
    image.so              found by type name
    clock.so
storage/                  flat, shared; referenced by scene arguments
    midnight.jpg
    logo.png
    clock/face.png        subdirectories allowed, purely organisational
```

Worked example — the scene list names `midnight`; `midnight.json` places two
instances, the second painting over the first; each instance carries its own
arguments; both resolve their files inside `storage/image/`:

```
scenes.json     ["midnight", "morning"]
  └── midnight.json
       ├── [0] instance "Background", type "image", background=midnight.jpg   ← bottom
       └── [1] instance "Logo",       type "image", background=logo.png       ← on top
                                                    origin_x=80, width_percent=15
```

- **FR-21** `[PoC]` — Scenes are declared in a text configuration file, one
  file per scene, looked up **by name in a well-known `scenes/` directory**.
- **FR-22** `[PoC]` — A scene definition declares: the scene name, the end
  condition (FR-17), and the **ordered** list of plugin instances. List order
  is display order (FR-14).
- **FR-23** `[PoC]` — Each plugin instance entry declares an instance name, a
  plugin type, and a set of arguments.
- **FR-24** `[PoC]` — Arguments are **opaque to the application**. They are
  passed through to `Init()` unmodified and interpreted entirely by the plugin.
- **FR-62** `[PoC]` — The configuration format is **JSON**. XML support may be
  added later if a need appears; nothing in the design depends on the choice,
  since only the loader sees it.

  > Line comments (`// …`) are accepted and stripped before parsing, so
  > hand-authored scenes can be annotated. This is the one thing strict JSON
  > lacks that XML has, and it is cheap to keep.

```json
{
  "name": "midnight",
  "end": { "type": "duration", "ms": 3600000 },
  "plugins": [
    {
      "instance": "Background",
      "type": "image",
      "arguments": {
        "background": "midnight.jpg",
        "origin_x": 0,
        "origin_y": 0,
        "width_percent": 100,
        "height_percent": 100
      }
    }
  ]
}
```

> Note that `width_percent` / `height_percent` here are *the image plugin's*
> convention, not the application's. FR-24 makes arguments opaque, so a plugin
> may use percentages, pixels or anything else internally; it converts to NDC
> (FR-13) when it emits draw items. See [OP-4](../project-plan/open-decisions.md#outstanding--proposed-defaults).

##### Plugin discovery and storage

- **FR-177** `[P2]` — Plugin files follow **standard Linux shared-library
  versioning**: a real file carrying the version, and symlinks narrowing to
  the unversioned name, as `libfoo.so.1.2.3` ← `libfoo.so.1` ← `libfoo.so`.
  Selection picks the newest version **compatible with the ABI version the
  application implements** (FR-33), not simply the newest present.
- **FR-178** `[PoC]` — Until then the PoC uses the flat convention below.
  Versioned naming is packaging, and packaging is not what the PoC is proving.
- **FR-179** `[PoC]` — A plugin's **fully qualified name is retrievable from
  the plugin itself** — the `name` field of `refapp_plugin` — so the filename
  is a convention for *finding* it, never the authority on *what it is*. The
  magic and version check (FR-33) confirms identity after load.
- **FR-25** `[PoC]` — Plugin shared objects are located **by filename
  convention**: `"type": "image"` resolves to `plugins/image.so`. The magic
  signature (FR-33) is verified after load to confirm identity.

  > **Not** by scanning every `.so` in the directory and querying each for its
  > name. Scanning means `dlopen`-ing every file present, which runs the static
  > initialisers of code the scene never asked for — a startup cost
  > proportional to the plugin count and an avoidable trust decision. If
  > decoupling plugin name from file name is ever wanted, the upgrade is a
  > one-time scan that builds a cached name → path index, with the convention
  > as the fast path. See [OP-15](../project-plan/open-decisions.md#outstanding--proposed-defaults).

- **FR-26** `[PoC]` — Argument values naming media resolve against the flat
  storage root (FR-61).
- **FR-27** `[PoC]` — Everything is local in the PoC: no network access at
  scene load.

#### 7a. Content distribution and updates

The long-term goal is content pushed from a server, up to and including
breaking news reaching the screen. That goal is reached in stages, and only the
first is in the PoC.

##### Definitions

| Term | Meaning |
| ---- | ------- |
| **Content set** | A coherent, versioned bundle: the scene list, every scene definition it names, and every media file those scenes reference. The unit of distribution and of activation. |
| **Activation** | The moment a complete content set replaces the running one. |
| **Lead time** | The interval between a content set arriving and its activation time — the window available for background downloading. |

##### Stages

| Stage | Phase | Behaviour |
| ----- | ----- | --------- |
| **1. Local** | `[PoC]` | Whatever is on disk. No server, no download, no activation logic. |
| **2. Scheduled distribution** | `[P2]` | Content sets arrive ahead of their activation time; media downloads in the background across the lead time. The normal path. |
| **3. Immediate update** | `[P2]` | The server demands activation now; everything downloads at once and activates as soon as it is complete, accepting a brief glitch. |
| **4. Live push** | `[L]` | Data reaches the screen without a scene restart, so urgent content appears with no glitch. Out of scope. |

##### Stage 2 — scheduled distribution

- **FR-68** `[P2]` — Resolution is triggered when the **scene list changes**,
  and on a periodic schedule (typically at midnight).
- **FR-69** `[P2]` — On trigger the system computes the **transitive set of
  media** referenced by every scene in the new scene list, diffs it against the
  flat storage root (FR-61), and downloads **only the difference**. Media
  already present because another scene uses it is not re-fetched.
- **FR-70** `[P2]` — Downloads run as a **background task** and must not
  disturb the frame rate of the content set currently playing.
- **FR-71** `[P2]` — A content set may carry an **activation time in the
  future**. Scenes for two days ahead can be delivered today, leaving roughly a
  day of lead time to download in the background.

##### Stage 3 — immediate update

- **FR-72** `[P2]` — The server may mark a content set for **immediate
  activation**. The system downloads at once rather than spreading the work
  across a lead time, accepting the bandwidth cost and possible frame-rate
  impact while it does so.

##### Activation, for both stages

- **FR-73** `[P2]` — Activation is **atomic and all-or-nothing**: a content set
  becomes active only when every scene definition *and* every media file it
  references is present. A partially downloaded set never plays.
- **FR-74** `[P2]` — Activation restarts the player on the new content set.
  When it cuts across a running scene (stage 3), **a brief visible glitch is
  accepted** and is within budget — the price of not having stage 4. Scheduled
  updates avoid it entirely by activating at a scene boundary; see FR-86.
- **FR-75** `[P2]` — A scene whose media is missing at play time is skipped and
  logged rather than started with holes. FR-73 should make this unreachable;
  it is defence in depth.

##### Staging, immutability and retention

Scenes, plugin arguments and media can all change at any moment, and a change
must never disturb what is playing. Three rules make that safe.

**1. Content sets are immutable.**

- **FR-80** `[P2]` — An installed content set is **never edited in place**. Any
  change — a scene, a plugin argument, an image — produces a **new version**.
  This is what makes "old data is not dropped" achievable at all: nothing
  overwrites what the running scene is reading.
- **FR-81** `[P2]` — **Plugin arguments version with their scene, and scenes
  version with their content set.** There is no independent lifecycle for an
  argument. Changing one argument yields a new content set version.

  > The alternative — versioning arguments separately from scenes — creates a
  > compatibility matrix between argument versions and scene versions that
  > someone has to reason about at activation time. Atomic versioning trades a
  > slightly larger download for not having that problem.

**2. Incoming content is staged, never live.**

- **FR-82** `[P2]` — Downloads land in a **staging area** that the player
  cannot see. A content set leaves staging only once every scene definition
  and every media file is present and validated (FR-73).
- **FR-83** `[P2]` — **Activation** promotes a staged content set to active.
  It is a discrete event, not a gradual one.

**3. Retention is by reference, and the running scene holds a reference.**

| State | Meaning | Eligible for cleanup |
| ----- | ------- | -------------------- |
| **Downloading** | Incomplete, in staging | No — and never playable |
| **Staged** | Complete, awaiting its activation time | No — pinned |
| **Active** | Currently playing | No — pinned |
| **Superseded** | Replaced, but the scene it started is still running | No — pinned until teardown |
| **Retired** | Nothing references it | Yes |

- **FR-84** `[P2]` — The active content set, every staged and future-dated set,
  and any superseded set still referenced by a **running** scene are all
  **pinned**. Cleanup never touches pinned data. A content set becomes retired
  only once the player has actually moved off it and torn its plugins down.
- **FR-85** `[P2]` — Media is **content-addressed**: the stored name derives
  from the content. Logical names used in plugin arguments (`midnight.jpg`) are
  resolved to a stored blob through the active content set's manifest.

  Three things fall out of this at once:

  | | |
  | - | - |
  | Media identical across versions | Stored once, not duplicated per version |
  | Media changed in a new version | Gets a new blob; the old one is untouched and still readable by the running scene |
  | Two authors both shipping `background.jpg` | No longer collide; the logical names differ per content set, the blobs differ by content |

- **FR-86** `[P2]` — Activation policy depends on urgency:

  | Update kind | When it activates | Cost |
  | ----------- | ----------------- | ---- |
  | Scheduled (stage 2) | At the **next scene boundary** | No glitch — the running scene finishes normally |
  | Immediate (stage 3) | **At once**, cutting the running scene | Brief glitch, accepted by FR-74 |

  > This is why FR-74's glitch is confined to stage 3. The normal path has a
  > natural seam — the end of a scene — and there is no reason to cut across
  > it.

##### Content set manifest

- **FR-87** `[P2]` — A content set is described by a manifest carrying at
  minimum:

| Field | Purpose |
| ----- | ------- |
| `version` | Identity of this content set. Immutable (FR-80). |
| `activation` | Absolute timestamp, or `immediate` (FR-72). Drives FR-86. |
| `expires` | Optional. When this set stops being eligible to play; lets a set retire without a replacement. |
| `scenes[]` | Scene definitions in this set, with their play order (FR-60). |
| `media[]` | For each: logical name, content address, size. Drives the download diff (FR-69), integrity validation (FR-82) and the reference count (FR-84). |

##### Retention — a cache, not a garbage collector

Data that stops being referenced is **not** deleted. Removing a scene from the
playlist makes its data *dangling*, and dangling data is kept for as long as
there is room for it. Deletion is driven by **storage pressure**, not by a
reference count reaching zero.

- **FR-76** `[P2]` — Media referenced by no installed content set becomes
  **dangling**. Because storage is flat and shared (FR-61), this is a reference
  count over the union of all installed sets, not a directory delete.
- **FR-77** `[P2]` — **Future-dated content sets count as live.** Media
  downloaded today for a set activating in two days is referenced, not garbage;
  deleting it would silently undo the lead time won by FR-71.
- **FR-140** `[P2]` — A scene leaving the playlist does not delete its data.
  The scene list is the player's input, and dropping a scene from it is
  routine — often temporary.
- **FR-141** `[P2]` — Dangling data carries a **time to live**, counted from
  when it stopped being referenced.
- **FR-142** `[P2]` — Dangling data sits in effect in **low-priority storage**:
  retained indefinitely while space allows, reclaimed only under pressure.
- **FR-143** `[P2]` — When free space falls below a threshold, dangling data is
  freed **least-likely-to-be-needed first**: TTL already exceeded before TTL
  still running, and within each, **least recently displayed** first.
- **FR-144** `[P2]` — When a scene re-enters the playlist, its data is looked
  for among dangling data first and **not re-downloaded** if the checksum
  matches.

> **FR-144 needs no special mechanism.** Under content addressing (FR-85)
> dangling blobs live in the same store as live ones, so the download diff of
> FR-69 finds them automatically — a re-added scene is simply a set whose blobs
> are already present. What FR-144 really requires is that dangling data has
> not been *eagerly* deleted, which is FR-140.

##### Identity

Retention and deduplication both need to name things stably.

- **FR-145** `[P2]` — Every scene, every plugin instance and every media item
  has a **unique identifier or mnemonic**, and the same identifier is used in
  the database and in filesystem paths. `plugin_storage://` (FR-137) expands
  through these identifiers.
- **FR-146** `[P2]` — A plugin instance's identifier is **assigned, not derived
  from its parameters**.

  > A derived identifier — hashing plugin type plus parameters — is tempting
  > because it deduplicates identical instances. It is wrong here: editing one
  > parameter would change the identifier, orphan everything under that
  > instance's storage, and re-download assets that had not changed. The
  > identifier must survive parameter edits, so it is assigned once and kept.

- **FR-147** `[P2]` — **Last-displayed timestamps**, which FR-143 sorts by, are
  observed by the Renderer and reported to the Updater over the IPC channel
  (FR-128). The Updater remains the only writer to storage.

  > Worth stating because it is the one place the Renderer produces data rather
  > than only consuming it. Routing it through IPC keeps FR-106 and FR-133
  > intact — one writer, no lock contention between the two applications over
  > who owns the store.

##### Notification and adoption

Who decides whether an update can be taken live: **the plugin**, not the data.

- **FR-111** `[P2]` — The Updater **notifies** the Renderer that new data is
  available. The Renderer does not poll, and the Updater does not act on the
  Renderer's behalf.
- **FR-112** `[P2]` — On notification, each running plugin instance is asked
  whether it can **adopt the new data in place** or **requires a restart**.
  The instance answers; the application obeys.
- **FR-113** `[P2]` — If every instance adopts, the scene continues
  uninterrupted. If any requires a restart, the scene is restarted at the
  point FR-86 allows — the next boundary for a scheduled update, immediately
  for a forced one.
- **FR-118** `[P2]` — An instance that does not implement the adoption
  entry point is treated as **requiring a restart**. Conservative by default:
  a plugin has to opt in to live update, never out of it.

> This supersedes the earlier idea of marking each *resource* updatable or
> not. The plugin is the only party that knows whether its data can be swapped
> mid-run — a slideshow can take a new image list between slides, a video
> decoder cannot re-seek without a visible break, and neither fact is a
> property of the file. Recording it on the resource would have meant the
> author guessing on the plugin's behalf. Resolves OP-19.

##### Commands

- **FR-119** `[P2]` — The server can push **commands** alongside update data,
  not only data. The first is **force-refresh**: stop whatever is running and
  restart it against the new data, overriding the per-instance adoption of
  FR-112.
- **FR-120** `[P2]` — Commands are handled by the Renderer, not forwarded to
  plugins. Force-refresh needs no plugin cooperation precisely because it
  destroys and recreates them.

##### Referential integrity

- **FR-121** `[P2]` — Ensuring **referential integrity** is the **Updater's**
  responsibility. The Renderer is told only once it holds.

- **FR-211** `[P2]` — A content set has referential integrity when **every
  reference in it resolves**:

  | Reference | Resolves to |
  | --------- | ----------- |
  | Each scene named in the scene list | A present, parseable scene definition |
  | Each plugin type named in a scene | A present `.so` whose magic and ABI version this application accepts (FR-33) |
  | Each storage URI in any argument | A present blob, in the tier the scheme names (FR-137) |
  | Each font referenced | Present under `system_storage://` |
  | Each `plugin_storage://` asset a plugin will fetch | Present, including assets selected indirectly by an enum parameter (FR-192) |

  Integrity is a property of the **whole set**, checked before publishing.
  A set missing one image is not "mostly fine" — it is not published.

- **FR-212** `[P2]` — A consequence worth relying on: **the plugins directory
  of a published set contains exactly the plugins its scenes name.** Nothing
  extraneous, nothing missing.
- **FR-122** `[P2]` — Downloads run as a **low-priority background task**, so
  a large update cannot starve the Renderer of I/O or CPU. This is the
  scheduling counterpart of FR-70, which covers frame rate.

> FR-121 is why the Renderer needs no partial-data handling at all. It is the
> same guarantee as atomic activation (FR-73), stated from the Updater's side
> and given an owner.

##### Slots, and not duplicating data

Running data and incoming data must coexist: the Renderer keeps reading what
it has while the Updater assembles what comes next. The obvious construction
is two **slots**, A and B — one live, one being filled — and the obvious
problem is that most content is unchanged between them, so a naive two-slot
layout stores it twice.

- **FR-123** `[P2]` — Running and incoming data sets coexist without the
  incoming one being visible to the Renderer (FR-82).
- **FR-124** `[P2]` — Unchanged content is **not duplicated** between them.

Two ways to satisfy FR-124:

| Approach | How | Cost |
| -------- | --- | ---- |
| **Content-addressed blob store**<br/>(already FR-85) | Slots are *manifests*, not directories. Both reference the same immutable blobs; only changed content becomes a new blob. | None beyond what FR-85 already requires. No privileges, no mounts, exact deduplication at file granularity. |
| **Overlay filesystem** | Physical slot directories, with the new slot an overlay whose lower layer is the current one. | Needs mount privileges or user namespaces; ties the data layout to a Linux kernel feature, which FR-115 (Android) and FR-3 (browser) do not share. Deduplicates only what the overlay's lower layer happens to contain. |

- **FR-125** `[P2]` — Deduplication is achieved by **content addressing**
  (FR-85), not by an overlay mount.

> The two solve the same problem, and content addressing was already required
> for other reasons — immutability (FR-80), collision-free naming, and
> reference-counted cleanup (FR-76). Under it "slots" stop being a storage
> construct and become what they already are in §7a: two content set versions,
> one active and one staged, sharing every blob they have in common. Overlay
> filesystems remain worth revisiting if profiling ever shows the blob store
> is the bottleneck — see [OP-20](../project-plan/open-decisions.md#outstanding--proposed-defaults).

##### Stage 4 — live push

- **FR-78** `[L]` — Plugins reload their data **in place**, without a scene
  restart, so urgent content (breaking news) appears without the FR-74 glitch.
  Explicitly out of scope: it pushes update semantics down into every plugin,
  and every plugin then has to be correct about it.
- **FR-79** `[L]` — A real-time push channel from the server, as the transport
  for FR-78.

> **Why stage 4 is deferred and not merely postponed.** Stages 2 and 3 keep all
> update logic in the application: plugins are created, run, and destroyed, and
> never observe an update. Stage 4 inverts that — every plugin becomes
> responsible for swapping its own data safely while rendering. That is a
> different contract, not an increment on this one, so nothing in the PoC
> should try to anticipate it beyond keeping the plugin API plain data
> ([R-1](../project-plan/risks.md)).

##### Scheduling

- **FR-28** `[P2]` — A schedule maps wall-clock time ranges to scenes, e.g.
  *from 00:00:00 to 01:00:00 run scene `midnight`*. At the boundary the running
  scene ends and the newly scheduled one starts.
- **FR-29** `[PoC]` — Without a schedule, the application plays a configured
  list of scenes in a loop. This is the PoC's driver.

##### Configuration updates

- **FR-30** `[P2]` — The scene or schedule definition may be updated while
  running. On update the current scene ends and is restarted from the new
  definition. This is the local, single-file case of the activation described
  in [section 7a](#7a-content-distribution-and-updates).
- **FR-31** `[P2]` — A **synchronisation service** downloads content sets —
  scene definitions, plugin shared objects and media — from a server into the
  local directories. It is the mechanism behind FR-68 – FR-77.

#### 8. Plugin contract

- **FR-32** `[PoC]` — **The application makes no assumption about a plugin's
  implementation language.** A plugin is a shared object exposing a **C API**,
  loaded on demand with `dlopen`.
- **FR-33** `[PoC]` — Plugins are identified by a magic signature so
  non-plugins and version mismatches are rejected at load.
- **FR-34** `[PoC]` — What happens *inside* a plugin is the plugin developer's
  choice: native C or C++, marshalling from C into Python or another language,
  or acting as an IPC client to a separate server process. None of this is
  visible to, or constrained by, the application.
- **FR-35** `[PoC]` — Each plugin exposes at least `Init()`, the frame query,
  and teardown.
- **FR-36** `[PoC]` — The frame query is called on the render thread and **must
  return within the frame budget**. Plugins doing slow work — language
  marshalling, IPC, network, decoding — must do it off the critical path and
  use the prefetch mechanism (FR-42) to have results ready. The application
  logs and skips a plugin that overruns.
- **FR-37** `[PoC]` — A reference plugin demonstrating in-plugin language
  marshalling. Superseded in detail by FR-64 and FR-66: the CPython embedding
  currently in `src/` becomes the Python boilerplate, and the Cairo clock is
  the worked example built on it.
- **FR-38** `[L]` — Browser-hosted plugins: WebAssembly, IPC, or a Cairo-like
  drawing API exposed through an interpreted layer that also maps onto the web
  canvas. Needs research.
- **FR-116** `[L]` — **Video.** Not in the PoC. When it arrives, hardware
  accelerated decode feeding GL rendering — GStreamer on Linux, or whatever
  the platform's VPU path is. The `REFAPP_PARAM_VIDEO` parameter type is
  reserved for it.

  > Video does not fit the texture model as it stands: FR-45 has the plugin
  > hand over an RGBA block per texture, which for 25–60 frames per second of
  > decoded video is the wrong shape entirely. A zero-copy path from decoder
  > to GL texture is the whole point of hardware decoding, and it needs the
  > application to accept a platform handle rather than pixels. That is a
  > change to the ABI, not an addition to a plugin, which is why this is `[L]`
  > and not `[P2]`.

- **FR-117** `[L]` — **Portable plugin drawing.** A single way to write a
  plugin's drawing code that works on native Cairo and on the web canvas
  alike. Research; it is the thing that would make FR-38 tractable rather
  than a rewrite per platform.

##### Draw item geometry

- **FR-63** `[PoC]` — A draw item is a **triangle list**: per-vertex position
  in NDC plus a per-vertex texture coordinate, drawn with `GL_TRIANGLES`
  against one texture id and one shader id.

  The protocol carries **no transform matrices**. Rotation, scale and
  translation are expressed by the plugin in the vertex positions it emits. A
  rotated quad is four corners the plugin has already rotated — which is what
  lets the clock hands turn (FR-66) without the application knowing anything
  about clocks.

##### Reference plugins and boilerplate

- **FR-64** `[PoC]` — A **Python plugin boilerplate** under
  `plugins/python/`: the C shim that satisfies FR-32 (a `.so` exporting the C
  API) and embeds CPython, a thin Python-side API, a template plugin to copy,
  and instructions. The CPython embedding currently in `src/` moves here.
- **FR-65** `[PoC]` — A **native C reference plugin** (`image`) — the trivial
  case: load one texture, display it. Its purpose is to prove the C ABI on its
  own, before the Python layer is introduced.
- **FR-66** `[PoC]` — A **Cairo clock example** written in Python against the
  boilerplate:

  | Element | Mechanism |
  | ------- | --------- |
  | Clock face / background | Drawn with Cairo into a surface, handed over as a texture blob |
  | Hour, minute, second hands | **One** hand texture, drawn once, emitted as three draw items |
  | Hand movement | Per-frame rotated vertex positions (FR-63); the texture never changes |

  This is the PoC's end-to-end validation case: it exercises in-plugin
  language marshalling (FR-34), Cairo-produced textures (FR-45, OP-14),
  static-texture-with-moving-geometry (FR-63), texture reuse across draw
  items, and the aspect-ratio handling of FR-139 — a clock is visibly wrong if
  the canvas stretches.

- **FR-67** `[PoC]` — The Python boilerplate's runtime dependencies
  (`pycairo`) are provisioned by `docker/install-deps.sh` behind a flag,
  consistent with FR-9.

##### The PoC plugin set

- **FR-114** `[PoC]` — Three reference plugins, chosen because between them
  they exercise every part of the architecture:

  | Plugin | Exercises |
  | ------ | --------- |
  | **Background image / slideshow** | Static texture, load-once-and-keep, and — for the slideshow — a timed sequence with prefetch of the next image ahead of its `needed_at_ms` |
  | **Analog clock** | Offscreen Cairo drawing, one hand texture reused across three draw items, per-frame rotated geometry over unchanged textures (FR-63, FR-66) |
  | **Scrolling marquee** | Offscreen Cairo text, geometry moving every frame, and texture wider than the screen clipped by UV |

  The slideshow is the one that genuinely exercises prefetch: a clock and a
  marquee load everything at scene start, whereas a slideshow must have image
  *n+1* resident before image *n* finishes.

##### Plugin development library

- **FR-88** `[PoC]` — A support library, **`libplugin_dev`**, is provided for
  plugin authors. It is **not** part of the plugin ABI: a plugin written in
  plain C against the raw API, using none of it, must remain valid. It exists
  to make third-party plugin development easy, not to constrain it.
- **FR-89** `[PoC]` — It is **statically linked** into each plugin. A shared
  `libplugin_dev` would put a second version-skew surface between application
  and plugin, next to the one FR-33 already guards.
- **FR-90** `[PoC]` — **All per-pixel work happens in the plugin's C layer,
  never in an interpreted language above it.** A Python plugin that performed a
  per-pixel format conversion in Python would be orders of magnitude too slow;
  the C shim exists to be the place such work lands.

  > This is a second, independent justification for the C shim. The first is
  > ABI compliance (FR-32). The second is that pixel loops must be compiled
  > code, whatever language the plugin's logic is written in.

- **FR-91** `[PoC]` — Cairo's output is accepted **without a CPU conversion
  pass**. Two distinct things are involved and they are solved differently:

  | | What it is | How it is handled |
  | - | ---------- | ----------------- |
  | **Premultiplied alpha** | Cairo's `ARGB32` has colour channels already multiplied by alpha | **Nothing to do.** The application blends `GL_ONE, GL_ONE_MINUS_SRC_ALPHA`, which is correct for premultiplied data (OP-14). |
  | **Channel order** | `ARGB32` in memory on a little-endian host is `B, G, R, A`; `GL_RGBA` expects `R, G, B, A` | **Swizzled in the shader**, not on the CPU (FR-208). |

- **FR-208** `[PoC]` — Byte order is corrected **in the fragment shader**. The
  bytes are uploaded unchanged, declared as `GL_RGBA`, and the shader samples
  `.bgra`.

  > This removes the whole-image traversal entirely. A GPU swizzle is free —
  > it is a register lane selection, not work — whereas a CPU pass over a
  > 1920×1080 texture touches 8 MB every time the texture is produced, which
  > for a streaming marquee is every tile.
  >
  > It also lands the channels correctly with no special case for alpha:
  > uploaded as-is, the shader sees `r=B, g=G, b=R, a=A`, so `.bgra` yields
  > `(R, G, B, A)` with alpha already in place.
  >
  > And unlike `GL_EXT_texture_format_BGRA8888` (FR-93) it needs no extension,
  > so it works everywhere including the legacy Broadcom stack.

- **FR-209** `[PoC]` — The application provides an identity shader **per
  accepted byte order**, and selects between them using the texture
  descriptor's format field (FR-47). Accepted formats are therefore
  `RGBA8_PREMUL` and `BGRA8_PREMUL`.

- **FR-210** `[PoC]` — `libplugin_dev` still offers a CPU swizzle, as a
  **fallback for plugins that cannot use the provided shaders** — one supplying
  its own shader later (FR-51), for instance. It is not the normal path.

- **FR-92** `[P2]` — Further `libplugin_dev` helpers, as the reference plugins
  show what is repetitive. Candidates: the magic-signature and version
  boilerplate (FR-33), argument lookup and type coercion (FR-24), quad and
  rotated-quad builders emitting NDC triangle lists (FR-63), and a logging
  shim that reaches the application's log.
- **FR-93** `[P2]` — Use `GL_EXT_texture_format_BGRA8888` to skip the FR-91
  swizzle where the driver advertises it, keeping the conversion as fallback.
  An optimisation, not a correctness requirement.

##### Load cost statistics

- **FR-94** `[PoC]` — The application maintains a table of **how long assets
  take to become resident**, as upper bounds by size: textures by maximum
  width and height, shaders by maximum line count and stage. Platform
  dependent, so measured rather than assumed.
- **FR-95** `[PoC]` — The table is **seeded with platform defaults** and passed
  to each plugin instance at `create`.
- **FR-96** `[PoC]` — The application **measures** actual load times as a scene
  runs, tracking the maximum per bound, and revises the table up or down.
- **FR-97** `[PoC]` — Revisions are pushed to plugin instances through an
  `update_stats` entry point.
- **FR-98** `[PoC]` — **The plugin decides how far ahead to request a load**,
  using the table to convert an asset's size into a lead time. The application
  schedules within the deadline it is given; it does not second-guess when a
  plugin should have asked.

##### Parameter reflection

- **FR-99** `[PoC]` — A plugin **declares the parameters it accepts** as static
  data on its descriptor: name, editor label, type, whether required, a
  default, and validation limits.
- **FR-100** `[PoC]` — Parameter types are:

  | Type | Encoding | Notes |
  | ---- | -------- | ----- |
  | `STRING` | verbatim | |
  | `TEXT` | verbatim, may contain newlines | multi-line |
  | `BOOL` | `true` / `false` | |
  | `INTEGER` | decimal, 64-bit signed | **One integer type only** — see FR-180 |
  | `REAL` | decimal, `.` separator, double precision | |
  | `DATETIME` | ISO 8601 with offset, e.g. `2026-08-09T13:20:00+03:00` | |
  | `TIME` | `HH:MM:SS` | time of day, no date |
  | `COLOR` | `#RRGGBB` or `#RRGGBBAA` | **One colour type** — alpha optional |
  | `FONT` | family name, or `system_storage://` URI | size is a separate parameter — FR-181 |
  | `IMAGE` | storage URI | |
  | `VIDEO` | storage URI | reserved, not in the PoC |
  | `ENUM` | one of the declared option values | plugin declares the options — FR-182 |

- **FR-180** `[PoC]` — There is **one integer type**, 64-bit signed, not a
  family of int32 / uint32 / int64 / uint64.

  > Width and signedness are **range constraints**, and the schema already
  > carries min and max (FR-101). `uint32` is `min: 0, max: 4294967295`. Four
  > types collapse into one plus a bound the editor can enforce anyway — and
  > the wire stays a decimal string, so there is no width to translate when a
  > Python or JavaScript plugin reads it. Which was the concern: "int — which
  > width, how does this translate to another language?" It does not have to,
  > if the ABI never names a width.
  >
  > Same reasoning collapses `float` and `double` into one `REAL`. The wire is
  > a decimal string, so precision is whatever the digits say.

- **FR-181** `[PoC]` — A `FONT` parameter names a family or a URI. **Size is a
  separate `INTEGER` or `REAL` parameter**, not bundled into the font value.

  > Bundling would make the limits of FR-101 inexpressible — there is no way to
  > say "size between 8 and 96" about half of a compound value — and sizes are
  > commonly edited independently of the family.

- **FR-182** `[PoC]` — A plugin may declare **its own enumerations**: a set of
  allowed values with editor labels. Line style dashed / dotted / solid,
  transition fade / cut / wipe.
- **FR-183** `[P2]` — An enum option may carry a **preview image**, so the web
  editor can show what each choice looks like rather than naming it.

- **FR-102** `[PoC]` — The schema is readable **without instantiating the
  plugin**, so tooling can inspect a shared object it never runs.
- **FR-103** `[PoC]` — Each parameter type fixes a **string encoding** — colours
  as `#RRGGBB` / `#RRGGBBAA`, date-times as ISO 8601, images as a logical media
  name — so a value round-trips between editor, scene file and plugin.
- **FR-104** `[L]` — A **server-side scene editor** consumes the schemas: a
  user drags a plugin into a scene, sees its parameters, and edits them with
  the widget and validation the declaration implies.
- **FR-105** `[L]` — The editor **generates the content set's media list**
  (FR-87) from the image-typed parameters and their values, so media is never
  declared by hand in two places.

> **Design constraint imposed by FR-34 and FR-38:** the frame-query payload
> must be a plain data contract — no host pointers except the texture blob, no
> callbacks back into the application, no shared ownership. This is what allows
> a plugin to be an IPC shim today and a WASM module later without the
> application noticing. See [R-1](../project-plan/risks.md).

#### 9. Per-frame protocol

- **FR-39** `[PoC]` — Once per frame the application queries each plugin
  instance in scene list order, passing the current timestamp.
- **FR-40** `[PoC]` — The timestamp is **milliseconds since the scene started**.
  Not wall clock, not a frame index. Wall clock appears only in the schedule
  (FR-28).
- **FR-159** `[PoC]` — The frame query also carries **wall-clock time**,
  alongside the scene-relative `frame_time_ms` of FR-40.

  > **Without this the analog clock plugin (FR-114) cannot work.** FR-40 gives
  > a plugin milliseconds since the scene started and deliberately withholds
  > wall clock, on the grounds that wall clock belongs to the schedule. That
  > holds for deciding *what plays*; it does not hold for content that is
  > *about* the time of day. A clock started in the middle of a scene has no
  > way to know where the hands go.
  >
  > A plugin could call `time()` itself — it is an ordinary process. That is
  > rejected: it makes the plugin an impure function of its inputs, so it can
  > no longer be tested deterministically ([test-plan §2](../test-plan.md)),
  > and it cannot be driven to a chosen moment to produce a golden image.
  > Passing the time in costs 8 bytes and keeps every plugin reproducible.
  >
  > The value is UTC; `system.timezone` (FR-156) converts it. That split keeps
  > the per-frame payload a number rather than a string, and lets the time zone
  > stay a create-time property.

- **FR-41** `[PoC]` — The response has three parts:

| # | Part | Contents |
| - | ---- | -------- |
| 1 | **Display set** | Draw items for this frame: vertices, texture id, shader id. May be empty. |
| 2 | **Residency changes** | Textures and shaders to load, and those to offload because they are no longer needed. |
| 3 | **Prefetch hints** | Textures needed later, each with a TTS. |

- **FR-42** `[PoC]` — The application attempts to load prefetch-hinted textures
  in advance of their TTS.
- **FR-43** `[PoC]` — TTS is on the same clock as FR-40: milliseconds since
  scene start.
- **FR-44** `[PoC]` — The response also carries the instance's termination
  flag, feeding the `all` / `any` end conditions (FR-17).

#### 10. Resource model

##### Textures

- **FR-45** `[PoC]` — A plugin supplies a texture as a **memory block plus
  width and height, in RGBA**. Producing it — decoding a file, drawing with
  Cairo, fetching from a server — is entirely the plugin's business.
- **FR-46** `[PoC]` — The application uploads that block to the GPU.
- **FR-47** `[PoC]` — The texture descriptor carries an explicit format field
  even though RGBA is the only accepted value now, so OUT-1 can be lifted
  without changing the contract.
- **FR-48** `[PoC]` — The application schedules uploads so they do not disturb
  the frame rate.
- **FR-49** `[L]` — Adaptive estimation of how long a plugin will take to
  produce or fetch a texture, so prefetch triggers early enough. Research.

##### Shaders

- **FR-50** `[PoC]` — The application creates the default shaders, including
  the identity shader, and assigns them **well-known ids** (0, 1, …). Plugins
  reference shaders by id.
- **FR-51** `[P2]` — Plugin-supplied shaders. The id indirection in FR-50 is
  what makes this additive.

##### Residency

- **FR-52** `[PoC]` — The application tracks what is resident on the GPU and
  honours explicit load/offload requests (FR-41 part 2).
- **FR-205** `[PoC]` — Each plugin instance is told **how much texture memory
  it may hold**, as a property. Machine-wide figures (`gl.memory_mb`, host
  memory) say what exists; this says what *this instance* may take.

  > Streaming content forces this. A marquee holds a sliding window of text
  > tiles and must decide how many — and a scene has several instances, each of
  > which could reason from the machine total and keep everything resident.

- **FR-206** `[PoC]` — **Budget, do not evict.** A plugin staying inside its
  allowance is never evicted. Eviction (FR-53) is a backstop against plugins
  that exceed it, not the primary mechanism.

  > Eviction as the primary mechanism fails badly here. The application would
  > reclaim a texture, the plugin would not be told, its next draw item would
  > name a resource that is gone, and by OP-9 the item is silently skipped. A
  > marquee would not fail loudly — it would develop gaps. Budgeting removes
  > that failure mode instead of handling it.

- **FR-207** `[PoC]` — A plugin exceeding its budget is **logged and its
  excess refused**, not silently starved. A refused `LOAD` is visible to the
  plugin author; a silently dropped one is a bug report from the field.

- **FR-53** `[L]` — GPU memory ceiling and automatic eviction. Unsolved; see
  [R-5](../project-plan/risks.md).
- **FR-54** `[L]` — Authoring-time warning, from the scene server, when a
  scene's peak texture footprint will not fit a target display.

#### 11. Threading

- **FR-55** `[PoC]` — Where the platform supports multiple GL contexts, texture
  upload may run on a second thread.
- **FR-56** `[PoC]` — Where it does not, uploads are interleaved between frames
  on the GL thread.
- **FR-57** `[PoC]` — Plugins may use their own threads internally. The frame
  query is called on the render thread and is bound by FR-36.

#### 12. Failure behaviour

- **FR-58** `[PoC]` — A plugin that fails to load, fails to initialise,
  misbehaves or overruns the frame budget is logged as an error.
- **FR-59** `[PoC]` — The application skips to the next scene on plugin
  failure. Since the scene loop wraps, a failing plugin in a single-scene
  configuration is re-created each time that scene restarts.

#### 12a. Non-functional requirements

No source states any of these. They are listed because several are needed as
**concrete numbers** before or during the PoC — a requirement that says "must
not disturb the frame rate" is not implementable until someone says what the
frame rate is.

| ID | Quantity | Needed by | Status |
| -- | -------- | --------- | ------ |
| **NFR-1** | Target frame rate, and the display refresh it is derived from | FR-36 `[PoC]` | **Unspecified.** Assumed 60 Hz vsync (OP-11). |
| **NFR-2** | Frame budget for the plugin frame query — the threshold at which a plugin is logged and skipped | FR-36 `[PoC]` | **Unspecified.** Derives from NFR-1; a fraction of the frame, not the whole of it. |
| **NFR-3** | Target display resolutions and aspect ratios | FR-13, FR-139 `[PoC]` | **Unspecified.** Affects texture sizing; aspect ratio itself is handled at run time. |
| **NFR-4** | GPU memory budget | FR-53, R-5 `[P2]` | **Unspecified.** The peak-footprint warning (FR-54) needs a ceiling to compare against. |
| **NFR-5** | Host memory budget | FR-45 `[P2]` | **Unspecified.** Plugins hand over full RGBA blobs; several at once is real memory. |
| **NFR-6** | Startup time to first frame | `[P2]` | **Unspecified.** |
| **NFR-7** | Expected plugin count per scene, and scene count per content set | FR-25, §7a `[P2]` | **Unspecified.** Decides whether plugin-discovery and activation costs matter. |
| **NFR-8** | Media volume per content set, and total storage budget | §7a `[P2]` | **Unspecified.** Sizes the lead time in FR-71 against available bandwidth. |
| **NFR-10** | Free-space threshold at which dangling data starts being reclaimed | FR-143 `[P2]` | **Unspecified.** Too low and an update fails for want of space; too high and the dangling cache never pays for itself. |
| **NFR-11** | Default time to live for dangling data | FR-141 `[P2]` | **Unspecified.** See OP-26 — a default, and whether the server can override it per scene. |
| **NFR-9** | Download bandwidth assumption | FR-71, FR-72 `[P2]` | **Unspecified.** "One day of lead time is enough" is a claim about this number. |

NFR-1 and NFR-2 are the only two that block PoC *code* — everything else can
be measured once something runs. Proposal for those two: 60 Hz vsync, plugin
frame-query budget 4 ms, revisited once the Cairo clock (FR-66) gives a real
measurement.

#### 12b. Turning failures into warnings

A missed deadline (OP-9) is handled at run time by skipping the draw item and
logging. That is the right *reaction*, and it is the wrong *goal*: by the time
it happens the audience has already seen the fault. The valuable version is
telling an author, at authoring time, that a scene will not run on a given
profile.

##### The model comes first, telemetry only refines it

- **FR-194** `[PoC]` — A **device assessment** program measures what this
  machine costs to render on, and produces the load-cost model that FR-95
  seeds plugins with. The model is measured on the device, not inferred from
  its spec sheet.
- **FR-195** `[P2]` — Assessment classifies the device into a **model class**,
  which is what a profile (FR-167) is derived from. Measurement is the
  authority; the hardware label is a label.
- **FR-196** `[P2]` — Field telemetry (FR-184) **refines an existing model, it
  does not build one.** A device is useful in the field only once it is already
  classified.
- **FR-197** `[P2]` — Field measurements and assessment measurements must
  define the **same quantity the same way** — same format, same call, same
  point of timing. Otherwise the two cannot be merged and refinement is
  arithmetic on incomparable numbers.

  > Easy to get wrong and hard to notice: a benchmark timing `glTexImage2D` to
  > completion and a field metric timing "submitted to drawn" are both
  > defensible, and averaging them is meaningless.

##### The texture ladder

- **FR-198** `[PoC]` — The baseline measurement is texture upload time against
  size, on a ladder halving in each dimension from full HD. FHD RGBA is
  **unity**:

  | Step | Pixels | Bytes, RGBA8 | Area vs unity |
  | ---- | ------ | ------------ | ------------- |
  | 1/1 | 1920 × 1080 | 7.91 MiB | 1 |
  | 1/2 | 960 × 540 | 1.98 MiB | 1/4 |
  | 1/4 | 480 × 270 | 506 KiB | 1/16 |
  | 1/8 | 240 × 135 | 127 KiB | 1/64 |
  | 1/16 | 120 × 68 | 32 KiB | 1/256 |

  > Halving each dimension quarters the area, so the ladder spans a factor of
  > 256 in bytes across five points. Worth being explicit about, because "1/2"
  > naming invites reading it as half the cost.

- **FR-199** `[PoC]` — The ladder is fitted to **fixed cost plus cost per
  byte**. The two coefficients are the model; a single number is not.

  > This is what makes the ladder worth measuring rather than timing one
  > texture. The intercept is per-upload overhead — driver call, state change,
  > synchronisation — and the slope is throughput. A device with a large
  > intercept and a device with a steep slope want opposite strategies, and one
  > measurement cannot tell them apart.

- **FR-200** `[PoC]` — **The ladder answers the tiling question.** Whether
  splitting a large texture into smaller pieces helps follows directly from
  the fitted coefficients: overhead-dominated devices are made worse by
  splitting, throughput-dominated devices are unaffected, and only a device
  showing a discontinuity — a size at which cost jumps — benefits. It is
  measured per device, never assumed.

- **FR-201** `[P2]` — Assessment also measures **`glTexSubImage2D`**, since
  partial update is the call a tiling strategy would actually use, and its
  cost profile differs from full upload.
- **FR-202** `[PoC]` — Assessment measures **shader compilation** against
  source length, feeding the shader half of the load-stat table (FR-94).

> Assessment covers **GPU** cost. The CPU cost of producing a texture — a
> plugin rasterising with Cairo — is not modelled and does not transfer from
> the texture ladder, because Cairo cost tracks what is drawn rather than how
> big the surface is. Open problem, with directions recorded in
> [../project-plan/research-notes.md](../project-plan/research-notes.md#1-cpu-cost-of-offscreen-rendering).

##### Capability-driven, not fixed

- **FR-203** `[P2]` — The assessment is a **declarative list of cases, each
  conditional on a capability**. A device advertising a compressed texture
  format gets those cases too; one that does not, skips them. New hardware
  brings new cases without a new assessment program.
- **FR-204** `[P2]` — Assessment re-runs when the ground moves: at
  provisioning, and after an OS, driver or firmware change. A model measured
  against a driver that has since been replaced is worse than no model,
  because it is trusted.

##### The mechanism already exists

- **FR-184** `[P2]` — The client records per-scene, per-instance measurements —
  missed deadlines, frame-budget overruns, actual upload latencies, peak
  texture memory — and reports them to the server through the Updater
  (FR-147's channel).
- **FR-185** `[P2]` — The server **aggregates by profile** (FR-167). "Scene
  *midnight* misses 4 deadlines a minute on profile *pi3*" is actionable;
  the same statement about one device is noise.
- **FR-186** `[P2]` — The **load statistics of FR-94 – FR-98 are reused as a
  design-time cost model.**

  > This is the part that costs almost nothing. That table already says what an
  > asset of a given size costs to make resident, per platform, measured rather
  > than guessed. At run time the client uses it to schedule. Sent back to the
  > server, the same table lets the editor compute — before deployment — that
  > this scene needs 210 MB of texture uploads in its first 2 seconds, and that
  > profile *pi3* can move 40 MB in that time. A runtime scheduler input
  > becomes an authoring-time predictor with no new instrumentation.

##### What the author sees

- **FR-187** `[P2]` — The editor grades a scene **per profile** — will run /
  marginal / will not run — and **names the limiting factor** rather than
  showing a score. "Peak texture memory 210 MB exceeds profile *pi3* budget of
  128 MB" tells the author what to change; a red badge does not.
- **FR-188** `[P2]` — Grading happens **while editing**, not on deployment.
  A warning that arrives after publishing has already cost a rollout.

##### Beyond prediction

- **FR-189** `[L]` — **Progressive degradation**: a scene may mark elements
  optional, and the client drops them under pressure instead of missing
  deadlines. Turns a hard failure into a graceful one for cases prediction
  cannot catch.
- **FR-190** `[L]` — **Canary rollout**: deploy to a few clients of a profile,
  watch FR-184 telemetry, then roll out fully. Catches what static prediction
  cannot — thermal throttling, a driver quirk, an unusual panel.
- **FR-191** `[L]` — **Reference-device dry run**: play a scene headless on one
  device per profile before publishing, and report what it measured. The most
  accurate predictor available, because it is not a prediction.

> The progression is deliberate: FR-186 predicts from a model, FR-191 measures
> on one device, FR-190 measures across a few, FR-189 copes when all three were
> wrong. Each is more expensive and more accurate than the last, and each is
> useful without the others.

#### 12c. Tools

The system ships utilities alongside the Renderer. They are part of the
deliverable, not scaffolding.

- **FR-218** `[PoC]` — **`refapp-assess`** — the device assessment of FR-194.
  A separate program, not a mode of the Renderer: it runs at provisioning and
  after a driver change, produces the load-cost model, and has no reason to be
  linked into the player. Scope is the texture ladder (FR-198) and shader
  compilation (FR-202).
- **FR-219** `[PoC]` — **`refapp-command`** — sends any Updater→Renderer
  command (FR-129) from a shell: `StorageUpdated`, `ForceRefresh`,
  `SuspendScreen`.

  > This is what stops `on_content_update` being dead weight until the Updater
  > exists. The entry point is declared in ABI v1 so that adding it later does
  > not break third-party plugins — and with this tool it is also *exercised*
  > from the first day, by hand and by the test suite, rather than shipped
  > untested and discovered broken in `[P2]`.

- **FR-220** `[PoC]` — **`refapp-conformance`** — the plugin ABI conformance
  harness ([test-plan §3](../test-plan.md)), shipped for third-party plugin
  authors to run before releasing.
- **FR-221** `[P2]` — **`refapp-new-plugin`** — boilerplate generator for C,
  C++ and Python plugins.

#### 13. Out of scope

| ID | Item | Phase |
| -- | ---- | ----- |
| OUT-1 | Compressed textures | `[P2]` |
| OUT-2 | Platform-specific optimisation; the common path is used everywhere | `[P2]` |
| OUT-3 | 3D, lighting, shading beyond sampling and placing a texture | not planned |
| OUT-4 | Plugin-supplied shaders | `[P2]`, see FR-51 |
| OUT-5 | Automatic GPU memory eviction | `[L]`, see FR-53 |
| OUT-6 | Wall-clock scheduling | `[P2]`, see FR-28 |
| OUT-7 | Configuration hot reload | `[P2]`, see FR-30 |
| OUT-8 | Content distribution: sync service, scheduled and immediate updates | `[P2]`, see §7a |
| OUT-10 | Live push; in-place plugin data reload without a scene restart | `[L]`, see FR-78 |
| OUT-11 | Scene authoring server | `[L]` |
| OUT-12 | Video | `[L]`, see FR-116 |

#### 14. Decision log

| Question | Decision | Landed in |
| -------- | -------- | --------- |
| GLES version | GLES2 | FR-10 |
| Raspberry Pi / DispmanX | **Reinstated** as a `[PoC]` target; third EGL backend | FR-126, FR-127 |
| Platform selection | X11 + Wayland, CMake flags, runtime auto-detect | FR-6, FR-7 |
| Vendor GL/GLES drivers | Documentation requirement | DOC-1 |
| "positional" plugin list | First = bottom-most, painter's order, alpha blended | FR-14, FR-15 |
| Response parts | Three, plus a termination flag | FR-41, FR-44 |
| Shaders | App-created identity shaders at well-known ids | FR-50 |
| Plugin mechanism | Language-agnostic; C API in a `dlopen`-ed shared object | FR-32 – FR-36 |
| In-plugin marshalling / IPC | Plugin developer's choice, invisible to the app | FR-34, R-2 |
| Plugin lifecycle | Per-scene create, `Init()`, destroy | FR-18, FR-20 |
| Scene definition | Text config per scene in `scenes/`, ordered plugin list | FR-21 – FR-24 |
| Scene end condition | `duration` / `all` / `any` / `never` | FR-17 |
| Plugin discovery | Filename convention `plugins/<type>.so`, not scan-and-query | FR-25, OP-15 |
| Media storage | One flat shared root; media reused across scenes stored once | FR-61, FR-26 |
| Media lifecycle | Periodic scene refresh, background fetch, reference-counted cleanup | FR-68 – FR-71 |
| Configuration format | JSON, line comments stripped | FR-62 |
| Draw item geometry | Triangle list, per-vertex NDC + UV, no transform matrices | FR-63 |
| Reference plugins | Native C `image`, Python boilerplate, Cairo clock example | FR-64 – FR-67 |
| Plugin dev library | `libplugin_dev`, static, optional, outside the ABI | FR-88, FR-89 |
| Pixel work | Always in the plugin's C layer, never in an interpreted language | FR-90 |
| Cairo pixel format | Premultiplied accepted as-is; byte order swizzled at texture load | FR-91, OP-14 |
| create / destroy | Synchronous; caller waits, no partial state | plugin-api.md §2 |
| Frame timestamp | Intended display time of the *next* frame | plugin-api.md §3 |
| Residency | One timeline of LOAD / KEEP / DROP ops per resource | plugin-api.md §4 |
| Geometry | Vertices are a resource; re-LOAD an id for dynamic geometry | plugin-api.md §4 |
| Load estimation | App measures costs, plugin decides lead time | FR-94 – FR-98 |
| Reflection | Plugins declare typed, bounded parameters as static data | FR-99 – FR-103 |
| Scene editor | Server-side, consumes schemas, generates the media list | FR-104, FR-105 |
| Prior implementation | EGL/GLES2 bring-up reusable; plugin model deliberately inverted | §13a |
| System decomposition | Four applications: Renderer, client Updater, server Updater, web interface | FR-106 – FR-109 |
| Renderer autonomy | Reads local storage only; never touches the network | FR-106 |
| Local storage | Database for arguments, filesystem for BLOBs, three URI schemes | FR-110, FR-135 – FR-138 |
| Update adoption | Updater notifies; each **plugin** decides live-adopt vs restart | FR-111 – FR-113, FR-118 |
| Server commands | Pushed alongside data; force-refresh overrides adoption | FR-119, FR-120 |
| Referential integrity | Updater's duty; Renderer told only when the set is complete | FR-121 |
| Slots and duplication | Manifests over a shared blob store, not overlayfs | FR-123 – FR-125, OP-20 |
| PoC plugin set | Background/slideshow, analog clock, scrolling marquee | FR-114 |
| Android, video, portable drawing | Long term, recorded for direction only | FR-115 – FR-117 |
| Delivery roadmap | Renderer → Updater → web interface → Android/preview → portable drawing | §1 |
| Scheduling | Wall-clock schedule deferred; PoC loops a scene list | FR-28, FR-29 |
| Config updates | Scene restarts on definition change | FR-30 |
| Content distribution | Versioned content sets; sync service promoted to `[P2]` | FR-31, §7a |
| Update trigger | Scene-list change, plus a periodic schedule (midnight) | FR-68 |
| Download strategy | Transitive media set, diffed against storage, difference only | FR-69 |
| Lead time | Content sets carry a future activation time; ~1 day to download | FR-71 |
| Immediate updates | Server can demand activation now, at bandwidth/FPS cost | FR-72 |
| Activation | Atomic, all-or-nothing; brief glitch accepted | FR-73, FR-74 |
| Retention | A cache, not a GC: dangling data survives until storage pressure | FR-76, FR-77, FR-140 – FR-144 |
| Eviction order | TTL exceeded first, then least recently displayed | FR-143 |
| Identity | Stable assigned IDs for scene, plugin instance and media, shared by DB and FS | FR-145, FR-146 |
| Immutability | Content sets never edited in place; any change is a new version | FR-80 |
| Argument lifecycle | Arguments version with their scene, scenes with their content set — atomic | FR-81 |
| Staging | Downloads land invisible to the player; promoted whole on activation | FR-82, FR-83 |
| Retention | Active, staged and still-running superseded sets are pinned from cleanup | FR-84 |
| Media addressing | Content-addressed blobs; logical names resolved via manifest | FR-85 |
| Activation timing | Scheduled at a scene boundary (no glitch); immediate cuts across | FR-86 |
| Manifest | version, activation, expires, scenes[], media[] with content addresses | FR-87 |
| Live push / in-place reload | Out of scope; different contract, not an increment | FR-78, OUT-10, R-12 |
| Timestamp | Milliseconds since scene start | FR-40 |
| TTS | Same clock as the frame timestamp | FR-43 |
| Coordinates | NDC `[-1, 1]` at the app boundary; config units are plugin-private | FR-13, OP-4 |
| Texture source | Plugin supplies RGBA blob + dimensions | FR-45 – FR-49 |
| GPU memory | Unsolved; deferred, server-side warning later | FR-53, FR-54, R-5 |
| Threading | Second thread where available, else interleave | FR-55 – FR-57 |
| Failure behaviour | Log, skip to next scene, re-create on scene start | FR-58, FR-59 |
