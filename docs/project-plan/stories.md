### PoC stories

The `[PoC]` scope, broken into deliverable units. Only the PoC — `[P2]` and
`[L]` work is not scheduled here.

Each story states what it delivers, which requirements it satisfies, and its
**acceptance criteria**: numbered, individually checkable, and phrased so that
whether one holds is a matter of observation rather than opinion. A story is
done when every criterion passes; a criterion that cannot be checked is not a
criterion.

Requirement identifiers refer to
[../architecture/requirements.md](../architecture/requirements.md).

| # | Story | Depends on | Status |
| - | ----- | ---------- | ------ |
| S-01 | Requirements and architecture baseline | — | **done** |
| S-02 | Raspberry Pi / DispmanX spike | S-01 | not started |
| S-03 | Surface layer — EGL over X11 and Wayland | S-01 | not started |
| S-04 | Plugin ABI: loader, conformance harness, native plugin | S-03 | not started |
| S-05 | Resource manager and device assessment | S-04 | not started |
| S-06 | Scene player and configuration | S-05 | not started |
| S-07 | Python plugin boilerplate | S-04 | not started |
| S-08 | Reference plugins: analog clock and marquee | S-06, S-07 | not started |

---

#### S-01 — Requirements and architecture baseline

**Delivers** a specification complete enough to build from, and the ABI header
everything else attaches to: requirements split into architecture and plan, the
plugin ABI with its rationale, build/style/SCM/test documents, rendered
diagrams, the container toolchain, and the CPython embedding demo the project
started from.

**Satisfies** — the documentation set; the ABI of `include/refapp/plugin.h`.

**Acceptance criteria**

| # | Criterion | How to check |
| - | --------- | ------------ |
| 1 | The ABI header compiles as C99 and as C++20 under `-Wall -Wextra -Wpedantic -Werror` | Build a minimal plugin and a host against it |
| 2 | A plugin built against it exports exactly one symbol and round-trips through `dlopen` | `nm -D --defined-only`, then `dlsym` |
| 3 | The demo application builds and its test passes in the container | `ctest --test-dir build-docker` |
| 4 | Every documentation link resolves, file and anchor | Link checker over `docs/` |
| 5 | No requirement identifier is defined twice | Duplicate scan over `requirements.md` |
| 6 | Every open point is dispositioned: resolved, scheduled, or deferred | `open-decisions.md` index totals match the table |

> All six hold as of the S-01 commit.

---

#### S-02 — Raspberry Pi / DispmanX spike

**Delivers** an answer to whether DispmanX stays, by attempting it rather than
arguing about it: port `platform-egl-context-dispmanx.c` from the prior
implementation, establish the build path, and get a frame onto a real Pi.

**Satisfies** — FR-126, FR-127. **Answers** OP-22, OP-23, OP-24.

**Acceptance criteria**

| # | Criterion | How to check |
| - | --------- | ------------ |
| 1 | A Raspberry Pi displays a cleared colour through the DispmanX backend | Look at the screen |
| 2 | The build path is reproducible by someone else from the written instructions | A second person follows `docs/building.md` and succeeds |
| 3 | `ENABLE_DISPMANX=ON` fails at **configure** time with a clear message when Broadcom headers are absent, not at link time | Configure in the x86 container |
| 4 | The Pi OS release and driver configuration used are recorded | `docs/building.md` names them |
| 5 | A recommendation on keeping or dropping DispmanX is written, with the evidence behind it | OP-24 moves to resolved |

> **First among the implementation stories, deliberately.** It is the task most
> likely to fail in a way that changes the plan, and R-4 notes that nothing in
> the container build exercises it — a green CI run says nothing about this
> target. It does not block S-03; run them in parallel if hardware allows.

---

#### S-03 — Surface layer: EGL over X11 and Wayland

**Delivers** a window and a GLES2 context on both desktop display servers,
behind one interface, selectable at build time and at run time.

**Satisfies** — FR-1, FR-2, FR-4 – FR-9.

**Acceptance criteria**

| # | Criterion | How to check |
| - | --------- | ------------ |
| 1 | One binary clears the screen under X11 and under Wayland | Run under both |
| 2 | Each backend builds with the other disabled | `-DENABLE_WAYLAND=OFF`, then the converse |
| 3 | Both disabled is a configure-time error | Configure with both `OFF` |
| 4 | Backend selection follows `WAYLAND_DISPLAY`, then `DISPLAY`, and a command-line flag overrides both | Unset each in turn; log names the chosen backend |
| 5 | The GL context reports OpenGL ES 2.0 | Log `glGetString(GL_VERSION)` at startup |
| 6 | No GLX symbol appears in the binary | `nm -D \| grep -i glx` is empty |

> Criterion 6 is not pedantry: FR-5 exists so that one GL path serves three
> surfaces, and a stray GLX dependency silently forfeits the Pi and the browser.

---

#### S-04 — Plugin ABI: loader, conformance harness, native plugin

**Delivers** the plugin boundary, proven without Python in the way: a `dlopen`
loader with magic and version checks, draw-item submission, a native C `image`
plugin written against the raw ABI, and the shipped conformance harness.

**Satisfies** — FR-25, FR-32 – FR-36, FR-50, FR-63, FR-65, FR-209, FR-220.

**Acceptance criteria**

| # | Criterion | How to check |
| - | --------- | ------------ |
| 1 | The `image` plugin loads, is instantiated, and draws a texture on screen | Look at the screen |
| 2 | The `image` plugin uses **no** `libplugin_dev` helper | Its build links nothing but the ABI header |
| 3 | A plugin with a wrong magic is rejected, naming the file and the reason | Corrupt the magic; read the log |
| 4 | A plugin with an unsupported ABI version is rejected the same way | Bump the version; read the log |
| 5 | A plugin overrunning the frame budget is logged and skipped, and the application keeps rendering | A test plugin sleeps past the budget |
| 6 | A rotated quad renders rotated, with no transform matrix in the protocol | Emit rotated vertices; observe |
| 7 | `refapp-conformance` passes against `image` and fails against a deliberately broken plugin | Run it against both |

> Criterion 2 is the mitigation for R-14. It is the standing test that the raw
> C ABI remains usable, and it is what stops `libplugin_dev` becoming a de-facto
> ABI.

---

#### S-05 — Resource manager and device assessment

**Delivers** residency with deadlines, per-instance memory budgets, and the
measured cost model that drives scheduling — including `refapp-assess`.

**Satisfies** — FR-41, FR-45 – FR-52, FR-94 – FR-98, FR-194 – FR-202,
FR-205 – FR-207, FR-218. **Mitigates** R-3, R-5. **Answers** part of OP-11.

**Acceptance criteria**

| # | Criterion | How to check |
| - | --------- | ------------ |
| 1 | `refapp-assess` produces two coefficients — fixed cost and cost per byte — from the texture ladder | Run it; read the output |
| 2 | Run on two different machines, the coefficients differ | Run on a desktop and on the Pi |
| 3 | Shader compilation time is measured against source length | Same output |
| 4 | Every upload passes through the queue, including the synchronous PoC path | No `glTexImage2D` outside the resource manager |
| 5 | Uploads are ordered by deadline, not by arrival | Submit out of order; observe ordering |
| 6 | A texture missing its deadline skips its draw item and logs **once**, not per frame | Starve one deliberately |
| 7 | An instance exceeding its memory budget has the excess **refused and logged**, not silently dropped | A test plugin over-allocates |
| 8 | `update_stats` revises the table mid-scene and a plugin observes the new values | Instrument a test plugin |

> Criterion 4 is R-3. The PoC may drain the queue synchronously, but everything
> must already go through it — retrofitting that later is invasive.

---

#### S-06 — Scene player and configuration

**Delivers** playback: scene list and scene files, plugin instances with
arguments and storage URIs, end conditions, the scene loop, and
`refapp-command`.

**Satisfies** — FR-16 – FR-31, FR-60 – FR-62, FR-135 – FR-138, FR-192, FR-219.
**Mitigates** R-11, R-13, and the teardown half of R-4.

**Acceptance criteria**

| # | Criterion | How to check |
| - | --------- | ------------ |
| 1 | **Two scenes loop**, indefinitely and in declared order | Run for several cycles |
| 2 | No GL object and no plugin state leaks between scenes | Object counts stable after 50 cycles; valgrind clean |
| 3 | Each of the four end conditions ends a scene: `duration`, `all`, `any`, `never` | One scene per condition |
| 4 | Plugin instances composite bottom-to-top in list order | Two overlapping instances; observe which is on top |
| 5 | All four storage URI schemes resolve, through **one** function | Grep: no other path construction |
| 6 | The scene list loads through a content-source abstraction, not a path in `main()` | Read the code |
| 7 | JSON line comments are stripped and do not reach the parser | A commented scene file loads |
| 8 | `refapp-command` delivers each Updater→Renderer command and the Renderer acts on it | Send each from a shell |

> Criterion 1 is not arbitrary. A PoC that runs one scene forever never
> exercises teardown, and content-set activation depends on teardown being
> right.

---

#### S-07 — Python plugin boilerplate

**Delivers** the language-marshalling path: a C shim exporting the ABI and
embedding CPython, a Python-side API, a template plugin, `libplugin_dev`, and
instructions.

**Satisfies** — FR-37, FR-64, FR-67, FR-88 – FR-91, FR-210.

**Acceptance criteria**

| # | Criterion | How to check |
| - | --------- | ------------ |
| 1 | A plugin whose logic is Python renders a texture | Look at the screen |
| 2 | The application links no Python library | `ldd` on the renderer binary |
| 3 | No per-pixel loop executes in Python | Profile, or read the code |
| 4 | An exception raised in Python never unwinds through the C ABI | Raise deliberately in `create` and in `frame` |
| 5 | The Cairo byte-order swizzle happens in the shader, not on the CPU | No whole-image traversal in the shim |
| 6 | `python_interpreter` conforms to the error-handling rules, and is renamed `.hpp` | Review against `coding-style.md` |
| 7 | A new plugin can be produced from the template by following the instructions alone | Someone else does it |

---

#### S-08 — Reference plugins: analog clock and marquee

**Delivers** the two plugins that exercise what the others do not: a Cairo
clock with rotating geometry over static textures, and a marquee streaming
text tiles, plus slideshow mode on the image plugin.

**Satisfies** — FR-66, FR-114, FR-139, FR-159, FR-208. **Informs** OP-33 and
research notes 1 – 3.

**Acceptance criteria**

| # | Criterion | How to check |
| - | --------- | ------------ |
| 1 | The clock face is **round on a 16:9 panel**, not elliptical | Look at it; measure if unsure |
| 2 | The hands show the correct local time, and survive a time-zone change | Set `system.timezone`; restart the scene |
| 3 | The clock uploads its textures **once**, then only changes geometry | Upload count stays flat after the first second |
| 4 | The marquee scrolls for ten minutes with no visible gap | Watch it |
| 5 | The marquee stays inside its memory budget throughout | Instrumented resident bytes never exceed the allowance |
| 6 | The marquee never requests a texture wider than `gl.max_texture_size` | Assert in the resource manager |
| 7 | The slideshow has image *n+1* resident before image *n* ends | Log residency transitions |
| 8 | All of the above hold on the slowest target, not only on a desktop | Run the same scene on the Pi |

> The marquee is the acceptance test for most of S-05. If tiling, deadlines or
> budgets are wrong anywhere, it shows up as visible gaps — criterion 4 is the
> one that will find them.
