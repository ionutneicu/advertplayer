### Test plan

How this project is verified. Requirement identifiers refer to
[architecture/requirements.md](architecture/requirements.md); the ABI is
[architecture/plugin-api.md](architecture/plugin-api.md).

#### 1. Principles

1. **A test states a behaviour, not an implementation.** If a refactor that
   changes no behaviour breaks a test, the test was wrong.
2. **Unit tests are fast, deterministic and isolated.** No filesystem, no
   network, no GL context, no sleeping, no wall clock. A unit suite that takes
   longer than a few seconds stops being run.
3. **One behaviour per test**, named for the behaviour:
   `SceneEndsWhenAnyInstanceFinishes`, not `TestScene3`.
4. **Arrange, act, assert** — in that order, visibly separated.
5. **A bug fix starts with a failing test.** The test is the evidence the bug
   existed; the fix is the evidence it is gone.
6. **Tests carry requirement identifiers.** A test for FR-17 says so, so that
   a requirement can be traced to the tests that cover it, the same way commits
   carry `Implements:` trailers ([scm.md](scm.md) §6).
7. **No test depends on another test's side effects**, or on execution order.

#### 2. What makes this system testable

Two design decisions, taken for other reasons, do most of the work:

- **A plugin is a pure function of state and time.** `frame` receives
  `frame_time_ms` and returns draw items and operations as plain data
  (plugin-api §3, §4). It touches no GL and no clock. A plugin can therefore be
  tested completely, deterministically, and without a display — drive it with a
  chosen time and assert the structures it returns.
- **The risk mitigations already require seams.** R-11 puts the scene list
  behind a content source, R-13 puts every media reference behind one
  resolution point, R-3 puts every upload behind a queue. Those exist to make
  future work cheap; they are also exactly the places a test substitutes a
  fake.

Where testability had to be designed in, it is recorded as a requirement
rather than left to chance.

#### 3. Levels

| Level | Scope | GL? | Runs in |
| ----- | ----- | --- | ------- |
| **Unit** | One class or function. Config parsing, scene end conditions, the load-stat lookup, NDC and aspect maths, residency bookkeeping. | No | Every build |
| **ABI conformance** | A plugin against the ABI contract: symbol, magic, version, call sequence, ownership, budget. | No | Every build; also shipped for third parties |
| **Component** | Real `dlopen`, real scene files, real resource manager — against a recording renderer instead of a GPU. | No | Every build |
| **System** | The whole Renderer against an offscreen EGL surface, compared to golden images. | Yes, offscreen | Every build where a GL stack is available |
| **Soak** | Long-running: scene looping, residency churn, leak detection. | Yes | Nightly |
| **Device** | The DispmanX backend and the Pi target, on real hardware. | Yes | Manually, and nightly once a Pi is wired into CI |

##### Unit

Pure logic only. The bulk of the suite and the fastest to write, because most
of the interesting behaviour is decisions rather than drawing: which scene ends
when, which upload must be issued by when, which draw item is skipped because
its resource missed its deadline.

##### ABI conformance

A harness that loads a plugin and asserts the contract holds:

- exactly one exported symbol, `refapp_plugin_entry`
- `magic` and `abi_version` correct; `create`, `frame`, `destroy` non-NULL
- `create` is synchronous and either succeeds or returns `REFAPP_ERROR`
- `frame` returns within the budget (NFR-2)
- `frame` never returns pointers into freed memory, and the same buffers may be
  reused across frames
- `vertex_count` is a multiple of three; ids referenced by draw items were
  `LOAD`ed
- a NULL `on_content_update` is treated as `NEEDS_RESTART` (FR-118)

This is the harness a third-party plugin author runs before shipping, so it is
a **deliverable**, not just internal scaffolding. The probe used to validate
the header is its starting point.

##### Component

Real plugin loading, real configuration, no GPU. The renderer is replaced by a
**recording renderer** that captures the draw call sequence, so assertions read
as "instance B's draw items were submitted after instance A's" rather than
poking at GL state.

##### System

Offscreen EGL — a pbuffer or surfaceless context — rendering to a buffer that
is compared against a golden image.

> **Software rendering is allowed in tests.** FR-4 forbids the *product* from
> depending on a software rasteriser; it says nothing about CI. Running the
> system tests on Mesa `llvmpipe` gives deterministic output on machines with
> no GPU, which is what makes golden-image comparison viable at all.

##### Device

The DispmanX backend (FR-126) cannot be built or run in the x86-64 container:
it needs Broadcom headers and a Raspberry Pi (FR-127). Everything above the
surface layer is covered by the other levels on x86, so device testing is
scoped to what only hardware can show:

- the backend brings up an EGL context on DispmanX and presents a frame
- the runtime backend selection of FR-7 picks DispmanX when no display-server
  variable is set
- frame rate holds at the target on real hardware (NFR-1), which is the honest
  reason this target exists — an x86 desktop will not reveal it

**A Raspberry Pi is PoC test hardware, not an afterthought.** Nothing in the
container build exercises FR-126, so a green CI run says nothing about it.

#### 4. Mocking

##### Prefer fakes to mocks

A **fake** is a working implementation with a simpler substrate — an in-memory
content source, a scripted clock. A **mock** asserts on calls. Fakes make tests
about outcomes; mocks make tests about implementation, and those are the tests
that break during refactors.

Use a mock only when the interaction *is* the behaviour under test — that
`update_stats` is called after a measurement, for instance.

##### The seams

| Seam | Fake | Lets you test |
| ---- | ---- | ------------- |
| Content source (R-11) | In-memory scene list and scene definitions | Scene sequencing, end conditions, restart on activation, without touching disk |
| Media resolver (R-13) | Map of logical name → fixture path or in-memory blob | Argument resolution and missing-media behaviour |
| Clock | Scripted `frame_time_ms` sequence | Prefetch lead times, TTS deadlines, scene duration — with no sleeping |
| Renderer | Recording renderer capturing draw items | Compositing order (FR-14), skipped draw items (OP-9) |
| Resource manager | Fake with a controllable upload latency | Deadline misses, KEEP/DROP, eviction |
| Surface layer | Headless stub | Everything above the GL boundary |

##### Two directions of test double

The plugin boundary is tested from both sides, and each side needs the other
stubbed:

- **Mock host, real plugin** — drives `create` / `frame` / `destroy` and
  asserts the returned timeline. This is how a plugin author tests their
  plugin, and what the conformance harness generalises.
- **Mock plugin, real application** — a small `.so` emitting a *scripted*
  sequence of draw items and operations. This is how the application's
  residency, ordering and failure handling are tested without depending on any
  real plugin's behaviour. Needs: a well-behaved plugin, one that overruns the
  budget, one that returns `REFAPP_ERROR`, one with a NULL
  `on_content_update`, and one that never finishes.

##### Do not mock the C standard library or GL

Mocking `glTexImage2D` tests that the code calls the function you expected, not
that it draws the right thing. Use the recording renderer one level up, or a
real offscreen context.

##### Hardware and driver matrix (OP-25)

A container has no GPU, so the build environment and the test environment are
not the same thing.

| Tier | Environment | Purpose |
| ---- | ----------- | ------- |
| **CI** | Mesa `llvmpipe`, software, in the container | Deterministic output, so golden-image comparison is meaningful. Catches logic, not drivers. |
| **Reference GPU** | One Intel or AMD machine on Mesa (`iris` / `radeonsi`) | The normal path for real users. Proves the code works on a real driver. |
| **Vendor driver** | One NVIDIA machine on the proprietary stack | The one materially different GL implementation, and what DOC-1 documents. |
| **Constrained device** | Raspberry Pi | Frame rate under real limits (NFR-1), and the surface backend of FR-126 |

Anything beyond these is opportunistic. The intent is to cover the *classes* of
GL implementation — software, Mesa, vendor, embedded — not to enumerate GPUs.

#### 5. Frameworks and layout

| Language | Framework | Notes |
| -------- | --------- | ----- |
| C++ | GoogleTest + GoogleMock | Matches the Google style baseline in [coding-style.md](coding-style.md) |
| C plugins | GoogleTest | The ABI is C; the test is C++ and includes the header through `extern "C"` |
| Python plugins | pytest | For the FR-64 boilerplate and the Cairo examples |

```
tests/
    unit/                    one file per unit under test
    conformance/             the shipped plugin ABI harness
    component/               dlopen + config + recording renderer
    system/                  offscreen EGL, golden images
    fakes/                   shared fakes: clock, content source, renderer
    plugins/                 scripted mock plugins built as .so
    data/                    fixtures and golden images
```

All registered with CTest, with labels so a developer can run a subset:

```bash
ctest --test-dir build-docker -L unit --output-on-failure
```

```bash
ctest --test-dir build-docker --output-on-failure
```

#### 6. Memory and undefined behaviour

The ABI's ownership rules (plugin-api §7) are exactly the kind of contract that
is silently violated and loudly crashes later, so they are checked mechanically:

- **AddressSanitizer + UndefinedBehaviorSanitizer** builds of the unit,
  conformance and component suites.
- **Valgrind** on the system suite, where sanitiser builds interact badly with
  GL drivers. The prior implementation already carried valgrind suppression
  tooling
  ([project-plan/prior-implementation.md](project-plan/prior-implementation.md));
  the suppression files are worth recovering rather than rewriting.
- A conformance case that **frees a plugin's returned buffers immediately after
  `frame` returns**, to prove the application copied what it kept.

#### 7. Coverage

Line coverage is a smoke alarm, not a goal — it finds untested files, not
untested behaviour.

| Area | Target |
| ---- | ------ |
| Scene, residency, configuration, load-stat logic | 90%+ |
| Plugin loader and ABI boundary | 90%+ |
| Surface and GL layer | Not measured; covered by system tests |
| Generated and third-party code | Excluded |

Measured with gcov/lcov in a dedicated build type, reported per merge request,
and **not** enforced as a hard gate — a coverage gate rewards tests that
execute code without asserting anything.

#### 8. Gates

Per [scm.md](scm.md) §4, a merge request is accepted only when the build and
the full suite pass. Concretely:

| Gate | When |
| ---- | ---- |
| Unit + conformance + component | Every push to a feature branch |
| System (offscreen GL) | Every push |
| Sanitiser build | Every push |
| Valgrind, soak | Nightly on `develop` |

A test that is flaky is either fixed or deleted in the same merge request.
Disabling it and moving on is how a suite stops being trusted.

#### 9. Scope by phase

| Phase | Tested |
| ----- | ------ |
| **`[PoC]`** | Unit, ABI conformance, component, system with golden images for the three reference plugins (FR-114). The conformance harness ships. |
| `[P2]` | Updater: referential integrity (FR-121), staging and activation (FR-73), adoption vs restart (FR-112), atomic switch under interruption. Content addressing and deduplication (FR-125). |
| `[L]` | Out of scope. |

#### 10. Current state

Honest baseline, to be replaced as the above is built:

- One CTest case, `python_host_smoke`, which runs the demo end to end and
  matches its output against a regular expression.
- An ABI conformance probe exists as scratch work — a plugin implementing all
  five entry points, compiled as C99 and C++20 under `-Werror` and exercised
  through `dlopen`. It is the seed of §3's conformance harness and should be
  promoted into `tests/conformance/` rather than rewritten.
- No unit, component or system suite yet. None of the seams in §4 exist,
  because the code they belong to does not exist either.
