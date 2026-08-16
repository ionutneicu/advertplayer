### Risks to the PoC

Every item that is not `[PoC]`, and what it would cost to retrofit if the
PoC ignores it. **Mitigation is what the PoC must do now** to keep that cost
low. Requirement identifiers refer to
[requirements.md](../architecture/requirements.md).

Each item that is not `[PoC]`, and what it would cost to retrofit if the PoC
ignores it. **Mitigation is what the PoC must do now** to keep that cost low.

| ID | Item | Phase | Risk | Why | Mitigation in the PoC |
| -- | ---- | ----- | ---- | --- | --------------------- |
| **R-1** | Browser / WebGL target | `[L]` | **High** | `dlopen` does not exist in a browser. If the plugin interface leaks host pointers or callbacks, no transport swap rescues it. | Keep the frame-query payload a serialisable plain-data contract (§8 note). Cost if ignored: full plugin API redesign. |
| **R-2** | Slow plugins (in-plugin IPC or language marshalling) | `[PoC]` | **High** | FR-34 explicitly permits a plugin to be an IPC client or a Python host. Either can block the render thread for tens of milliseconds and destroy the frame rate. | Enforce FR-36 from day one: time the frame query, log and skip overruns. Make prefetch the sanctioned way to do slow work. |
| **R-3** | Multi-threaded texture upload | `[PoC]`/`[P2]` | **Medium** | A synchronous upload buried in the draw loop is invasive to make asynchronous later. | Route every upload through an explicit queue, even if the PoC drains it synchronously between frames. |
| **R-4** | Raspberry Pi / DispmanX | `[PoC]` | **Medium** | Now in PoC scope (FR-126). The backend itself is cheap because it is EGL like the others, but it cannot be built or tested in the x86-64 container — it needs Broadcom headers and a device. Nothing about it is exercised by the normal build. | Keep the surface layer behind one interface so DispmanX is a third implementation, not a fork. Port the prior implementation's backend rather than writing one. Plan for on-device build and test from the start (FR-127, OP-23) — discovering it at integration time is the expensive path. |
| **R-5** | GPU memory ceiling and eviction | `[L]` | **Medium** | Retrofitting a residency manager into code that uploads ad hoc is a rewrite. | Ship a minimal resource manager (FR-52) with explicit load/unload, trivial policy. |
| **R-6** | Wall-clock scheduling | `[P2]` | **Low** | Additive layer above the scene player. | Keep scene selection behind a small interface; the PoC's loop and a schedule are two implementations of it. |
| **R-7** | Compressed textures | `[P2]` | **Low** | Only the upload path changes. | Explicit format field (FR-47). |
| **R-8** | Plugin-supplied shaders | `[P2]` | **Low** | Additive. | Shader-id indirection (FR-50). |
| **R-9** | Adaptive prefetch timing | `[L]` | **Low** | Pure policy over an existing mechanism. | Honour TTS naively. |
| **R-10** | Plugin ABI versioning | `[P2]` | **Low** | Third-party plugins built against an older header would crash. | Put a version number next to the magic signature (FR-33) and reject mismatches at load. |
| **R-11** | Content-set activation (§7a) | `[P2]` | **Medium** | Activation restarts the player on a new content set at an arbitrary moment. A PoC that reads its config once in `main()` and hardcodes paths has no seam to do that through. | Load the scene list through a single **content source** abstraction, and make "restart the player from the content source" an operation that exists in the PoC — the scene loop already needs it (R-4). |
| **R-12** | Live push / in-place plugin reload | `[L]` | **Low for the PoC, high for plugins** | Inverts the update contract: every plugin becomes responsible for swapping its own data while rendering. Not an increment on stages 2–3. | None beyond R-1. Do not anticipate it; the PoC's create/run/destroy plugin lifecycle is the right shape for stages 1–3. |
| **R-14** | `libplugin_dev` hardening into a de-facto ABI | `[P2]` | **Low–Medium** | If the application ever assumes plugins were built with it — a struct laid out by a helper, a handle only it produces — FR-32's language-agnosticism is quietly lost, and a plain-C plugin stops being viable. | Static linking (FR-89) plus one rule: the raw C API must be exercised directly by at least one reference plugin. FR-65's native `image` plugin is that test, so keep it helper-free. |
| **R-13** | Content addressing and staging (FR-80 – FR-87) | `[P2]` | **Medium** | Media resolution moves from "open this path" to "look this logical name up in the active manifest, then open that blob". If plugin-facing code or the loader spreads raw path handling around, that indirection has to be retrofitted everywhere. | Resolve every media reference through **one** function in the PoC, even though stage 1 just concatenates a path. Plugins receive resolved handles, never construct paths themselves. |

R-1, R-2, R-3, R-4 and R-5 are mitigated by requirements already in the `[PoC]`
set. No roadmap item threatens the PoC's structure.
