### Open decisions

Decisions taken and decisions outstanding. Identifiers refer to
[requirements.md](../architecture/requirements.md) and
[plugin-api.md](../architecture/plugin-api.md).

#### Resolved

| Was | Resolution |
| --- | ---------- |
| **Plugin mechanism** | Language-agnostic. Shared object + C API + `dlopen`; marshalling or IPC inside the plugin is the developer's choice. FR-32 – FR-36. |
| **Raspberry Pi / DispmanX** | **Reinstated as a `[PoC]` target**, on the strength of the working backend in the prior implementation. Three EGL backends: X11, Wayland, DispmanX. FR-1, FR-2, FR-126. |
| **Scene definition** | Text config per scene in a well-known directory: name, end condition, ordered plugin instance list with opaque arguments. Plugins found by type name; each has a storage folder. FR-21 – FR-27. |
| **Configuration format** | JSON, with line comments stripped before parsing. XML only if a need appears. FR-62. |
| **The existing CPython host** | Moves out of the core binary into the Python plugin boilerplate. The application never embeds Python. FR-64. |
| **Data lifecycle under change** | Immutable versioned content sets, staged then activated, with reference-based retention so nothing a running scene reads is ever dropped or overwritten. FR-80 – FR-87. |
| **Media name collisions** | Dissolved by content addressing (FR-85) rather than solved by a naming policy. |
| **Aspect ratio** (was OP-5) | Resolved by FR-139: the plugin receives the display aspect ratio and lays out for the real shape. No letterboxing, no distortion, resolution independence preserved. |
| **OP-3** | Convention `plugins/<type>.so` for the PoC. Versioned naming follows standard Linux shared-library practice later (FR-177); the fully qualified name is retrievable from the plugin itself (FR-179). |
| **OP-4** | Coordinates stay screen-agnostic. NDC at the ABI, config units plugin-private. NDC now defined in the glossary. |
| **OP-7** | Allowed. Each instance carries its own id/mnemonic from the server — `bg_pic`, `logo_pic` (FR-145). |
| **OP-8** | Strings across the ABI, typed accessors in `libplugin_dev`. Type list settled: one `INTEGER` and one `REAL` rather than width variants, one `COLOR`, ISO-8601 `DATETIME`, font and size as separate parameters, plus plugin-declared `ENUM` (FR-100, FR-180 – FR-183). |
| **OP-9** | Skip and log at run time; the real answer is predicting it at authoring time (§12b, FR-184 – FR-191). |
| **OP-10** | ABI resource ids are per plugin instance. Storage is a separate question, now four tiers with `plugin_storage://` per *type* for non-configurable assets (FR-137, FR-192, FR-193). |
| **OP-12** | Explicit `scenes/scenes.json`. A scan can reconstruct *membership* given referential integrity, but not play **order**, and the scene list is where further fields will accumulate — so an extensible text format, not a directory listing. |
| **OP-14** | Two separate problems, separately solved. Premultiplied alpha is accepted as-is and blended `GL_ONE, GL_ONE_MINUS_SRC_ALPHA` — no conversion. Channel order is swizzled **in the shader** (FR-208), not by a CPU pass over the image. |
| **OP-15** | Filename convention for the PoC. Scan-and-query moves to `[P2]` (FR-177): with referential integrity defined (FR-211, FR-212), the plugins directory contains exactly the scenes' plugins, so the objection to scanning weakens — it becomes a packaging choice rather than a correctness one. |
| **OP-17** | Direct paths in stage 1, behind the single resolver R-13 already requires. Content addressing solves problems stage 1 does not have — no Updater, no second content set, nothing to deduplicate — and it makes development harder, since `ls storage/` shows hashes instead of `midnight.jpg`. The migration is one function's body. |
| **OP-20** | Content addressing, not an overlay mount. |
| **OP-21** | Yes, in ABI v1. And `refapp-command` (FR-219) drives the IPC commands from a shell, so the entry point is exercised from day one rather than shipped untested. |
| **OP-29** | Extensible enumeration (FR-216), same mechanism as plugin-declared enums. The PoC defines `16:9` and `OTHER`; more members are additive because unrecognised values already fall back to the exact ratio. |
| **OP-30** | The seeded list. Split into observed and configured classes (FR-213); which component owns the canonical set stays open — see OP-36. |
| **OP-31** | No arbitrary rotation in the renderer. But tilt is now expressible: system properties split into **observed** and **server-configured** (FR-213, FR-214), and `install.tilt_angle` is the latter. |
| **OP-34** | Assessment becomes a separate shipped tool, `refapp-assess` (FR-218), scoped to the texture ladder and shader compilation. Tools are a deliverable set (§12c), not scaffolding. |
| **OP-35** | **Unresolved by design.** Not equal shares. Moved to [research-notes.md §3](research-notes.md#3-apportioning-memory-between-plugin-instances) with the planning-sweep direction. The PoC needs *an* apportionment, not the right one, and FR-205 keeps the policy behind a property so it can change without touching plugins. |

#### Index by disposition

Three kinds of open point. Only the first needs you.

| | Disposition | Count |
| - | ----------- | ----- |
| **A** | **Decide now** — design choices, no investigation required | 2 |
| **B** | **Resolve during the PoC** — needs measurement or hardware, not opinion | 6 |
| **C** | **Defer to `[P2]`** — no bearing on PoC work | 2 |

##### A — decide now

| ID | Question, in short | Proposed |
| -- | ------------------ | -------- |
| OP-28 | Server-side authoring vs plugin aspect awareness | Both; they solve different problems |
| OP-32 | Profiles derived or assigned | Derived from reported capabilities |

##### B — resolve during the PoC

These need a measurement or a device, not a decision. Each becomes a PoC
investigation with a provisional value to build against meanwhile.

| ID | Question | Provisional | Settled by |
| -- | -------- | ----------- | ---------- |
| OP-11 | Frame rate target, vsync (NFR-1) | 60 Hz, vsync-driven | Measuring on the slowest target |
| OP-33 | Raster size for procedurally-drawn plugins | `display.reference_canvas` property | Looking at the clock and marquee on a real panel |
| OP-24 | Is legacy DispmanX worth its cost | Attempt the port; drop it if it fights back | Porting the prior backend once |
| OP-22 | Which Raspberry Pi OS for DispmanX | Newest release where it still works | Trying it on the device |
| OP-23 | How the DispmanX backend is built | On-device build | The first build attempt |
| OP-25 | Test hardware and driver matrix | Software Mesa, one Mesa GPU, one NVIDIA, one Pi | What hardware actually exists |

> OP-22, OP-23 and OP-24 are one investigation, not three: porting the prior
> DispmanX backend once answers all of them. It is the first PoC task that can
> fail in an interesting way, so it should be attempted early rather than last.

##### C — defer to `[P2]`

| ID | Question | Why it can wait |
| -- | -------- | --------------- |
| OP-26 | Dangling data TTL | The Updater does not exist in the PoC |
| OP-27 | Free-space reclamation threshold | Same |

#### Outstanding — proposed defaults

None blocks the PoC. Each is recorded so the choice is deliberate, and each is
cheap to change later because it sits at the edges, not in the plugin ABI or
the render loop.

| ID | Open point | Proposed default |
| -- | ---------- | ---------------- |

| **OP-11** | **Frame rate target and vsync.** | vsync-driven, at whatever the surface reports. Frame budget for FR-36 derived from it. |
| **OP-22** | **Which Raspberry Pi OS for DispmanX?** It was removed from the default stack in Bullseye. Buster or earlier has it natively; Bullseye and later need the legacy driver enabled. | Target the newest release on which DispmanX is still available with the legacy graphics driver, and record the exact image used. Pinning to Buster would tie the PoC to an OS that is already out of support. |
| **OP-23** | **How is the DispmanX backend built and tested?** The x86-64 container cannot supply `libbcm_host` or the Broadcom headers (FR-127). | Build on the device for the PoC — simplest, and the PoC needs a Pi to test on regardless. A cross-toolchain plus a Pi sysroot is faster per iteration and worth doing once the backend stops changing daily. Either way, one Pi must exist as PoC test hardware. |
| **OP-24** | **Is legacy DispmanX worth its cost?** Reopened at the author's request. The aim behind it is proving the system runs acceptably on constrained hardware — not the legacy stack as such. | **Drop DispmanX; target a current Raspberry Pi on KMS/DRM + Mesa v3d.** Costs and gains are set out below. Keep DispmanX as `[L]`, recoverable from the prior implementation if genuinely old devices ever matter. |
| **OP-25** | **What hardware and drivers are tested on?** Building in Docker is settled (FR-9); testing is not, since a container has no GPU. | A three-tier matrix — see [../test-plan.md](../test-plan.md): Mesa `llvmpipe` in CI for determinism, one Intel or AMD machine on Mesa, one NVIDIA machine on the proprietary stack, plus the Pi. |

#### OP-24 in detail — the cost of DispmanX

| Keeping it costs | |
| ---------------- | - |
| **A second build and test path** | It cannot be compiled or run in the x86-64 container (FR-127). Every change needs on-device build or a sysroot. This is the dominant cost, and it is permanent. |
| **An out-of-support OS** | DispmanX means Buster or a later release forced onto the legacy driver. No security updates on Buster. |
| **Probably no threaded uploads** | Shared EGL contexts are unreliable on the legacy Broadcom stack, so FR-38 likely degrades to the interleaved path (FR-39) on this target. A real functional compromise, not just a build inconvenience. |
| **A third surface backend** | Mitigated: the prior implementation has a working one to port. |
| It does **not** cost the GLES version | GLES2 is already decided (FR-10) for other reasons. |

| Dropping it gains | |
| ----------------- | - |
| **One build path** | A current Pi builds against the same Mesa and EGL as the desktop. KMS/DRM is an EGL platform, so it is a small backend, not a fork. |
| **A supported OS** | Current Raspberry Pi OS, with updates. |
| **Headroom** | Pi 4/5 v3d supports GLES 3.1 (compute shaders) and Vulkan through `v3dv`. None of it is needed now, and all of it is available if this ever outgrows GLES2. |
| **Working shared contexts** | Mesa v3d supports them, so FR-38 stays available. |
| It **loses** | Proof that the system runs on genuinely old hardware. |

The stated aim was *"a good proof that the system is optimized enough to run at
least lightweight scenes on some old HW"*. A Raspberry Pi is constrained
hardware whether or not it is running the legacy stack — a Pi Zero 2 W or a
Pi 3 on current Pi OS demonstrates the same thing. **DispmanX was the means, not
the goal**, and the goal survives dropping it.

> The three claims about newer Pi capability — GLES 3.1, Vulkan via `v3dv`,
> working shared contexts — are from documentation, not measured here. Verify
> on the actual device before relying on any of them.
| **OP-26** | **What sets the dangling TTL (FR-141)?** A single client-side default, or a per-scene hint the server sends? | A client default, overridable per scene by the server. The server knows which scenes are seasonal and will return, and which are one-off — the client cannot guess that. |
| **OP-27** | **What free-space threshold triggers reclamation (FR-143, NFR-10)?** | A percentage of the storage volume rather than an absolute figure, evaluated before each download so a large update can force reclamation ahead of running out mid-transfer. |
| **OP-28** | **Does per-display authoring (FR-151) replace telling the plugin the aspect ratio (FR-139), or complement it?** | **Complement.** Keep both — see the analysis below. |

#### OP-28 in detail — who handles aspect ratio

Three candidate models:

| Model | How | Consequence |
| ----- | --- | ----------- |
| **A — plugin knows aspect** | Aspect ratio in `create_info`; the plugin lays out for the real shape. | One content set plays correctly on any panel. Costs one float in the ABI. |
| **B — server authors per display** | Client reports its display; the server sends parameters already adjusted. Plugins stay aspect-blind. | Content becomes display-specific: a scene shown on two panel shapes needs two parameter sets, and an unexpected panel renders wrong with no way to recover. |
| **C — both** | B for layout decisions, A for geometric correctness. | Each mechanism does what only it can. |

**C is the recommendation**, because A and B are not substitutes:

- **A cannot express a design change.** A portrait panel often wants a
  different composition, not a rotated one. Only an author can decide that,
  which is B.
- **B cannot guarantee geometric correctness.** For the clock to stay round the
  author would have to pre-compute a non-square quad for every panel shape in
  the fleet, and get it right every time. One unanticipated panel and the clock
  is an ellipse. A plugin given the aspect ratio simply cannot get it wrong.

Two facts that constrain the choice:

1. **The Renderer knows the aspect ratio no matter what.** It sets the viewport
   and the projection matrix. "The Renderer should not be aware" is not
   available; the only question is whether it passes the value to plugins.
   Withholding it does not remove the problem, it relocates it into the
   authoring tool.
2. **B needs a fleet inventory to exist first.** The server can only author per
   display once clients report what they have (FR-150), which is `[P2]` work.
   A `[PoC]` with no server has nothing to author against, so the PoC needs A
   regardless.
| **OP-32** | **Are profiles derived from reported capabilities, or assigned?** (FR-168) | Derived. A hand-assigned profile is a guess that goes stale the moment a device is updated, and the fleet will drift. Assignment can still exist as an override for a device that needs pinning. |
| **OP-33** | **At what pixel size do procedurally-drawn plugins rasterise?** FR-170 puts resolution-dependent work on the server and FR-13 keeps resolution from the client — which settles images, but not the Cairo clock and marquee (FR-114), which rasterise on the client at run time and must pick *some* size. | Publish a **`display.reference_canvas`** property: the pixel size the application wants offscreen content drawn at, derived from the effective geometry but expressed as a quality target rather than a resolution. A plugin draws at that size and the GPU scales if it must. It keeps FR-13 intact — nothing about *layout* depends on it — while giving procedural content a defensible answer instead of a hardcoded guess. |
| **OP-36** | **Which component owns the canonical system-property set?** The Renderer publishes them to plugins; the Updater reports them to the server (FR-171). Both read the same facts. | Left open. One owner queries and the other consumes, but which way round depends on whether configured properties (FR-214) arrive through the Updater — which they probably do, making the Updater the owner and the Renderer a consumer. Decide when the Updater is built. |
