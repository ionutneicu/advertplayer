### Requirements — open-question round (superseded)

**Superseded by [requirements.md](requirements.md).** Kept verbatim for
traceability: this is the question set with the answers written inline against
each one. Do not edit; make changes in `requirements.md`.

Status: **superseded**. Compiled from the original unstructured
`requirements.txt`, since removed. [requirements.md](requirements.md) is the
source of record.

Requirements are given identifiers (`FR-n` functional, `NFR-n` non-functional,
`OUT-n` explicitly out of scope, `OQ-n` open question) so they can be
referenced from code, commits and merge requests. Anything the source text did
not state is either marked *(inferred)* or raised in
[section 8](#8-open-questions) rather than silently decided.

#### 1. Purpose and scope

A Linux application that renders 2D content through native OpenGL drivers, and
delegates *what* to render to a set of plugins queried once per frame.

The application owns the GL context, the render loop and GPU memory. Plugins
own the content. The application asks each plugin, every frame, what it wants
on screen; the plugin answers with drawing work plus hints about resources it
will need later.

This first delivery is a **happy path**: the minimum that demonstrates the
architecture end to end, with optimisation and edge cases deliberately
deferred ([section 7](#7-out-of-scope-for-this-delivery)).

#### 2. Target platforms

| ID | Platform | Windowing / surface | Driver |
| -- | -------- | ------------------- | ------ |
| FR-1 | Native Linux desktop | X11 and Wayland | Mesa, or a GPU-specific driver |
| FR-2 | Raspberry Pi | dispmanx | Broadcom |

- **FR-3** — The application must work with native OpenGL drivers. It must not
  depend on a software rasteriser.
- **FR-4** — Platform selection (X11 / Wayland / dispmanx) must be resolvable
  without changes to plugin code.

> dispmanx is the legacy Broadcom display API. It was removed from the default
> Raspberry Pi OS graphics stack from Bullseye onward, which uses KMS/DRM with
> Mesa v3d instead. Which Pi models and OS releases are in scope decides
> whether dispmanx is the right target at all — see [OQ-2](#8-open-questions).

#### 3. Definitions

| Term | Meaning                                                                                                                       |
| ---- |-------------------------------------------------------------------------------------------------------------------------------|
| **Plugin** | A unit that contributes content. Initialised at startup, queried once per frame.                                              |
| **Frame query** | The per-frame call to a plugin, carrying the current /*TBD next frame desired*/ timestamp.                                    |
| **Draw item** | One request to display a portion of a texture at a location on screen, with a given shader.                                   |
| **TTS** (time to screen) | A plugin's hint of when a not-yet-loaded texture will be needed, so the application can /*TBD: schedule*/ load it in advance. |
| **Identity shader** | The default pass-through shader created at startup; samples a texture and writes it unmodified.                               |

#### 4. Application lifecycle

- **FR-5** — On startup the application initialises the GL context for the
  active platform (OpenGL ES; X11, Wayland or dispmanx surface). 
  /*TBD: decide whether this is autodetected, there will be a config or cmdline arg for selecting it
         what about missing libs on some platforms ?*/ 
- **FR-6** — It creates the default shaders /*TBD required for minimal OpenGL app*/, including an identity
  pass-through shader.
- /*TBD - on startup, the app looks in the scene folder. A scene has a start time and duration ( indirectly calculated )
- up to the next scene's start time*
- The system loads scenes from disk, i.e. JSON file.
- The system monitor changes to that file - either inotify watch added or IPC to sync services that
- actually mades updates from a server
- each scene has a list of plugins made of shader lib + arguments in a JSON
- arguments JSON contain simple values, where BLOB - image, text, video are
- on special media folder.
- the sync service will assure that for each scene, all required artifacts are downloaded
- and will issue a signal to the application that scene is ready to be played - TBD*/
- **FR-7** — It loads the plugins and initialises each by calling `Init()`.
- **FR-8** — It enters the render loop and, per frame, performs the frame
  query of [section 5](#5-per-frame-protocol) against every plugin.
- **FR-9** — Plugins are held in a **positional list**. The position carries
  meaning — see [OQ-3](#8-open-questions) for what kind.

Shutdown, teardown and plugin de-initialisation are not described by the
source text — see [OQ-7](#8-open-questions).

#### 5. Per-frame protocol

- **FR-10** — Once per frame, the application queries each plugin in list
  order, passing the current timestamp: *"we are at this timestamp, give me
  display things."* /*TBD - not making any differences from plugin perspective, 
- but the timestamp could be from future. */
- **FR-11** — A plugin's response has three parts:

| # | Part | Meaning |
| - | ---- | ------- |
| 1 | **Display set** | What to draw this frame: which vertices, using which loaded shaders and textures — i.e. which portion of which texture appears where on screen, with which shader. |
| 2 | **Residency changes** | Resources to load, and resources to offload from GPU memory because they are no longer needed (textures, shaders). |
| 3 | **Prefetch hints** | Textures needed at some future point, each with a TTS. |

- **FR-12** — The application acts on prefetch hints by attempting to load
  those textures **in advance** of their TTS.
- **FR-13** — A plugin declares which shaders to compile and load, which
  textures to compile and load, and which vertices to display given the
  loaded shaders and textures.

> The source text says "the plugin will respond with two things" and then
> lists three. Three is taken as correct — see [OQ-4](#8-open-questions).

#### 6. Rendering model

- **FR-14** — Rendering is **2D only**: clip a region of a texture and display
  it on screen.
- **FR-15** — Geometry uses the common primitive path, `GL_TRIANGLES`.
- **FR-16** — The GL API is OpenGL ES. The version — 2 or 3 — is explicitly
  undecided in the source text; see [OQ-1](#8-open-questions).

#### 7. Out of scope for this delivery

Deliberately excluded, to keep the first delivery a happy path:

- **OUT-1** — Compressed textures.
- **OUT-2** — Platform-specific optimisation. The common path is used
  everywhere (e.g. `GL_TRIANGLES`).
- **OUT-3** — 3D. No lighting, no shading beyond what is needed to sample and
  place a texture.

> OUT-3 sits awkwardly with FR-13, which has plugins choosing shaders per draw
> item, and with FR-6's default shader set. The reading taken here is that the
> shader *pipeline* exists and is plugin-selectable, but the shipped shaders
> are trivial samplers. See [OQ-5](#8-open-questions).

#### 8. Open questions

Blocking questions first. Each must be resolved before the affected
requirement can be implemented.

| ID | Question | Affects |
| -- | -------- | ------- |
| **OQ-1** | **Which OpenGL ES version?** The source says "OpenGL ES 2, 3 — the most suitable — TBD". GLES2 is the common denominator across desktop Mesa and legacy Broadcom; GLES3 adds VAOs, instancing, NPOT textures and `GL_RED`-style formats that make a 2D compositor materially simpler. Targeting both platforms with one code path likely forces GLES2. | FR-16, all rendering |
- lets stick to GLES2.
| **OQ-2** | **What is the Raspberry Pi target, concretely?** Which models and which OS release? dispmanx is unavailable on the current Pi OS graphics stack, so if the target is Pi 4/5 on Bookworm the second platform is really KMS/DRM + Mesa, not dispmanx — which changes FR-2 and possibly makes it converge with FR-1. | FR-2, FR-4 |
- i want reasonably new Raspberry PI, but let's stick on the latest ?
- also want to document at least how to use native drivers, i.e. Ati, nVidia when available, rather than mesa.
| **OQ-3** | **What does "positional" mean for the plugin list?** Draw order / z-order (later plugins paint over earlier)? A screen region assigned per plugin? Or just a stable iteration order? | FR-9 |
- it means the first plugin's textures is displayed Bottom most, first plugin in top(most) of previous ones
- transparency will be supported via alpha channel, so on the screen, each plugin's ouptut will blend
| **OQ-4** | **Two response parts or three?** The source says "two things" then lists three. Assumed three. Confirm. | FR-11 |
- three, apart shaders, we have texture and vertices. we can start with default shaders created by app, having index 0,1 etc.
- shader support to be added later
| **OQ-5** | **How do "no shading" (OUT-3) and per-draw-item shader selection (FR-13) reconcile?** Assumed: plugins may select among simple sampling shaders, and the default is the identity shader. | FR-13, OUT-3 |
- we use default shaders ( identity ) created by the main app and assigned to a well known shader id, i.e. 0 or 1
| **OQ-6** | **What is a plugin, mechanically?** Shared objects loaded with `dlopen`? Statically linked at build time? Python modules through the CPython embedding already in `src/`? This decides the entire plugin ABI and is the single largest open item. | FR-7, FR-9, FR-10 |
- yes, dinamically loaded libraries with dlopen + a magic signature.
- they are C, but aim for other languages, probably by adding GPB and IPC this is to be added in later phase
| **OQ-7** | **Plugin lifecycle beyond `Init()`.** Is there a shutdown/teardown call? Can plugins be added, removed or reloaded at run time, or is the set fixed at startup? | FR-7 |
- A scene is displayed at a time. A scene has either a finite time, or infinite ( never ends ) or when all plugins say end. 
- At beginning of the scene, all its plugins are initialized, but not all plugins are effective in each fame,
- as they can return an empty list of things to be displayed, however, they can prepare some things in advance,
- i.e. textures to be loaded in GPU and shaders, but these wont be visible
| **OQ-8** | **Timestamp semantics.** Wall clock, monotonic clock, or frame index? What unit? Is there a target frame rate, and is the loop vsync-driven or free-running? | FR-10 |
- I would say time since scene starts, in fine grained units to fall in every frame, say milliseconds
| **OQ-9** | **TTS units and contract.** Absolute timestamp or a duration from now? Is it a hint the application may ignore, or a guarantee the plugin relies on? What happens when a texture is requested for display before its prefetch completed — skip the draw item, stall, or draw a placeholder? | FR-12 |
- see above
| **OQ-10** | **Screen coordinates.** In what space does a plugin express "where on screen" — pixels, normalised device coordinates, or a virtual canvas scaled to the surface? How are differing resolutions and aspect ratios between the two platforms handled? | FR-11, FR-14 |
- i would say opengl coordinates [-1, 1], but something more precise can fit.
  - it should be regardless screen resolution ( plugin might not know that )
  | **OQ-11** | **Texture sources and formats.** Where do textures come from — files on disk, memory supplied by the plugin, somewhere else? Which formats must be decoded (PNG, JPEG, raw)? Note "compile and load" is used for textures as well as shaders; for textures this is assumed to mean decode and upload. | FR-13 |
  - It's plugin problem. It can either draw them using Cairo, or load from file, etc.
  - It will provide hem as a memory zone, RGBA + with + height it's the pluging duty to convert to this well known binary
  - format in memory, so OpenGL could load and maybe compress it ina convenient format.
  - what we need to take care of is the timing issue. providing the in-memory data can be done
  - by the plugin on dfferent thread, but when loading, this might take time, so has to be done in advance.
  - the plugin will take all the measures to trigger drawint in advance, for this, this adaptative algorithm to
  - figure out how much will take to draw a texture or download from a server or so on, to be researched in a later step
  - for applicaiton point of view, it should schedule loads in a way that won't affect FPS
  | **OQ-12** | **GPU memory budget and eviction.** Is there a memory ceiling? If prefetching would exceed it, does the application evict on its own, or only ever offload what a plugin explicitly releases (FR-11 part 2)? | FR-12 |
  - clearly a problem. designer of the scene might not be aware that all the plugins texture won't fit in GPU at their peak
  - on the server that will be added later, the scene designer should be warned that some displays won't be able to show it
  - also texture compression might help
  | **OQ-13** | **Threading model.** Is the render loop single-threaded with plugins called on the GL thread? Prefetch implies background loading — is texture upload done on a shared context from a worker thread, or staged and uploaded on the GL thread? | FR-12, NFR |
  - it the platform accepts multipe OpenGL threads, loading textures can be done on a different thread, otherwise
  - they need to be interleaved between frames
  | **OQ-14** | **Alpha and overdraw.** Do draw items blend, or is it opaque paint-over? What resolves overlap between two plugins' items — list position (OQ-3), an explicit z, or submission order? | FR-11, FR-14 |
  - Is this already responded
  | **OQ-15** | **Failure behaviour.** What happens when a plugin fails to initialise, throws, returns garbage, or blocks past the frame deadline — skip it for that frame, drop it permanently, or abort? | FR-7, FR-10 |
  - At least log some error, skip to next scene, but there could be one scene.... this means the plugin will be reinitialized upon scene init, 
  - as the system will loop through the scenes as well
  | **OQ-16** | **Relationship to the existing code.** `src/` currently holds a CPython embedding host (`PythonInterpreter`, `python_host`). Is that the intended plugin mechanism (OQ-6), a separate experiment, or scaffolding to be replaced? | Whole document |
  - the PoC is with Python, native calls, but the overall aim is to run it also in a web page, using WebGL.
  - we have 2 options -> use IPC for plugin, i.e. GPB
  - use webb assembly
  - expose Cairo drawing API through an itermediary interpreted language to be mapped also on Web's canvas
  - this has to be researched.
#### 9. Non-functional requirements

The source text states none. Candidates that would normally be pinned down at
this stage, all currently unanswered:

- Target frame rate and resolution per platform (see OQ-8).
- GPU and host memory budgets, especially on Raspberry Pi (see OQ-12).
- Startup time budget.
- Plugin count expected in a realistic deployment.
~~~~~~~~