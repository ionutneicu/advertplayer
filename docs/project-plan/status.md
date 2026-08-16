### Status

> ## Ready to start the PoC
>
> The three blockers from the previous round are resolved: the plugin
> mechanism is language-agnostic C-over-`dlopen`, the platform set is X11 and
> Wayland plus Raspberry Pi via DispmanX, and scenes are declarative config in a
> well-known directory.
>
117 functional requirements: **73 `[PoC]`**, 30 `[P2]`, 10 `[L]` (plus four
stated as rows in the platform table). And 9 non-functional (§12a), of which 7
are still unquantified.

> The `[PoC]` requirement set is complete and internally consistent, and every
> roadmap item's mitigation is already inside it. The twelve remaining open
> points all have proposed defaults, all sit at the edges — config format,
> resource layout, id scoping — and none would force rework of the plugin ABI
> or the render loop if reversed.
>
> Three things to watch during the PoC, all already requirements rather than
> open questions:
>
> - **R-2, slow plugins.** FR-34 permits a plugin to marshal into Python or
>   talk IPC. Either can blow the frame budget. FR-36's timing-and-skip is not
>   optional garnish; build it with the first plugin, not after.
> - **R-4 and R-11, clean teardown and a restart seam.** Loop at least two
>   scenes in the PoC, and load the scene list through a content-source
>   abstraction rather than reading a path in `main()`. A PoC that runs one
>   scene forever exercises neither, and content-set activation (§7a) is
>   exactly "tear everything down and restart from the source" at an arbitrary
>   moment.
> - **R-13, one media resolution point.** Every media reference goes through a
>   single function, and plugins receive resolved handles rather than building
>   paths. Stage 1 just concatenates a path; staging and content addressing
>   (FR-85) then slot in behind it instead of being retrofitted across the
>   loader and every plugin.
>
> Overriding any of the §16 defaults is a cheap change if done now — say which
> before implementation starts.
>
> **Two numbers are needed before PoC code**: NFR-1 (target frame rate) and
> NFR-2 (plugin frame-query budget). Proposed 60 Hz and 4 ms. Every other
> non-functional requirement in §12a can be measured once something runs.
>
> **The prior implementation** ([§13a](prior-implementation.md)) supplies a
> working EGL + GLES2 bring-up to start the surface layer from, and settles
> nothing else: its plugin model is inverted relative to this one, its dynamic
> loading was never implemented, and its Cairo plugin does not exist.

##### What this document does not contain

Deliberate omissions, so a reviewer knows what not to look for:

- **The plugin C API itself.** FR-32 – FR-36 and FR-63 specify what it must do
  and what shape it must have (plain data, no callbacks, no host pointers).
  The actual header — structs, function signatures, the magic-signature and
  version layout — is the first design deliverable of implementation, not a
  requirement. The same goes for `libplugin_dev`'s surface (FR-88 – FR-93):
  what it must contain is specified, its signatures are not.
- **Module and file layout of the application.** Requirements say what must be
  true, not how the source tree is arranged.
- **Choice of JSON library, EGL wrapper, or logging framework.**
- **The server side.** Content sets are described from the player's point of
  view: what it receives and how it behaves. How the server produces or
  publishes them is out of scope here.

---

#### Readiness review — all open points

Re-checked against the full open-point set. **Nothing outstanding blocks the
PoC.**

| Disposition | Points | Blocks PoC? |
| ----------- | ------ | ----------- |
| Resolved | 20 | — |
| PoC investigations, with provisional values | OP-11, OP-22, OP-23, OP-24, OP-25, OP-33 | No — each is scheduled inside a story and has a value to build against |
| Deferred to `[P2]` | OP-26, OP-27, OP-36 | No — all concern the Updater, which the PoC does not contain |
| Held open by the author | OP-28, OP-32 | No — OP-28 has a PoC decision (landscape 16:9); OP-32 is server-side |

The two that were expensive to reverse are settled: **OP-15** (plugin discovery
by convention) and **OP-21** (`on_content_update` in ABI v1, with
`refapp-command` exercising it from day one). Nothing else in the set would
force rework of the ABI or the render loop if reversed.

> ### Verdict: the PoC can start
>
> Work is broken into eight stories in [stories.md](stories.md). S-01 is
> complete.
>
> Two things about the sequencing are deliberate:
>
> - **S-02, the Raspberry Pi spike, comes first among the implementation
>   stories.** It is the task most likely to fail in a way that changes the
>   plan, nothing in the container build exercises it, and it answers three
>   open points by being attempted. It does not block S-03.
> - **S-04 uses a helper-free native plugin.** The raw C ABI has to be proven
>   usable before `libplugin_dev` exists, or the library quietly becomes the
>   ABI (R-14).
>
> Three structural commitments carry through every story, and are cheap now and
> expensive later: every upload through one queue (R-3), every media reference
> through one resolver (R-13), and the scene list behind a content source
> (R-11).
