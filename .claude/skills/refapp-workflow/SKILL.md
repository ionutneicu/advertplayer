---
name: refapp-workflow
description: The working agreement for opengl-refapp — how to branch, commit and open a merge request, how stories and acceptance criteria are written, how requirement identifiers are used, and how to verify a change in the container. Use when making any change to this repository, or when asked how work is organised here.
---

# opengl-refapp working agreement

How work is done in this repository. The rules are short; the reasons are in
the documents they point at.

## 1. Where things live

| Question | Document |
| -------- | -------- |
| What must the system do? | `docs/architecture/requirements.md` |
| What is the plugin ABI, and why? | `docs/architecture/plugin-api.md`, `include/refapp/plugin.h` |
| When is it being built, in what order? | `docs/project-plan/roadmap.md`, `stories.md` |
| What is still undecided? | `docs/project-plan/open-decisions.md` |
| What will hurt later if ignored now? | `docs/project-plan/risks.md` |
| Problems not yet specifiable | `docs/project-plan/research-notes.md` |
| How to build | `docs/building.md` |
| How to test | `docs/test-plan.md` |
| Code style, error handling | `docs/coding-style.md` |
| Branching and merge requests | `docs/scm.md` |

**Architecture answers *what*. Project plan answers *when* and *what is
undecided*.** Keep new material on the right side of that line.

## 2. Never commit to `develop`

Every change reaches `develop` through a merge request from a branch.

```bash
git switch develop && git pull --ff-only
git switch -c feature/<short-hyphenated-name>
```

Keep the branch current by **rebasing**, never merging, so the diff stays
honest:

```bash
git fetch origin && git rebase origin/develop
```

Force-push is acceptable only on your own unmerged feature branch.

## 3. Commit messages

```
<area>: <imperative summary, 72 chars or fewer>

Why the change is needed, and anything a reader could not infer from the
diff. Wrapped at 72 columns.

Implements: FR-25, FR-33
Refs: S-04
```

`<area>` is the part of the tree affected: `loader`, `renderer`, `egl`,
`plugin-api`, `docs`, `docker`, `tests`.

- Imperative mood: "add plugin loader", not "added".
- Explain **why**, not what.
- `Implements:` carries requirement identifiers, so a requirement traces to the
  commits that satisfied it. `Refs:` carries the story.

## 4. Stories and acceptance criteria

Work is organised into stories in `docs/project-plan/stories.md`. A story has:

- **Delivers** — one paragraph, plainly
- **Satisfies** — the requirement identifiers it implements
- **Acceptance criteria** — numbered, each with *how to check*

A criterion must be settled by observation, not opinion. "The clock is round on
a 16:9 panel" is a criterion; "the clock looks good" is not. **A criterion that
cannot be checked is not a criterion** — either make it checkable or drop it.

A story is done when every criterion passes. Not most.

## 5. Requirement identifiers are the currency

`FR-n` functional, `NFR-n` non-functional, `OUT-n` out of scope, `DOC-n`
documentation — all defined in `docs/architecture/requirements.md`. `OP-n` open
points and `R-n` risks are defined in `docs/project-plan/`.

- Every requirement carries a phase tag: `[PoC]`, `[P2]`, `[L]`. **The tags are
  binding** — do not implement `[P2]` or `[L]` work inside a `[PoC]` task.
- Changing behaviour means changing the requirement **in the same merge
  request**. Code and specification must not drift apart between requests.
- Discovering something undecided means adding an `OP-n` with a proposed
  default, not choosing silently.

## 6. Verify before opening a merge request

Everything builds in the container; the host needs only Docker.

```bash
docker build -t opengl-refapp-build docker/
```

```bash
docker run --rm -v "$PWD:/workspace" -u "$(id -u):$(id -g)" opengl-refapp-build cmake -S . -B build-docker -DCMAKE_BUILD_TYPE=RelWithDebInfo
```

```bash
docker run --rm -v "$PWD:/workspace" -u "$(id -u):$(id -g)" opengl-refapp-build cmake --build build-docker -j"$(nproc)"
```

```bash
docker run --rm -v "$PWD:/workspace" -u "$(id -u):$(id -g)" opengl-refapp-build ctest --test-dir build-docker --output-on-failure
```

Use `build-docker/`, never `build/` — CMake caches absolute paths and the
detected compiler, so host and container trees cannot be shared.

**A green container build says nothing about the Raspberry Pi target.** Nothing
in it exercises the DispmanX backend. Say so rather than implying coverage.

### Touching the ABI

`include/refapp/plugin.h` is the contract. After any change, check it still
compiles as both languages and still round-trips:

```bash
gcc -std=c99 -Wall -Wextra -Wpedantic -Werror -Iinclude -fPIC -shared -o probe.so probe.c
```

```bash
g++ -std=c++20 -Wall -Wextra -Wpedantic -Werror -Iinclude -o host host.cc -ldl
```

A plugin must export exactly one symbol: `nm -D --defined-only probe.so`.

**Adding a function pointer to `refapp_plugin` is a breaking change** — it
invalidates every third-party plugin (R-10). Prefer declaring an entry point
early and leaving it NULL-able over adding one later.

### Touching the documents

Links are checked, file and anchor. GitHub's anchor algorithm lowercases,
**removes** anything that is not alphanumeric, space or hyphen, then turns
spaces into hyphens — so `5.5 Compile, test and run` is `#55-compile-test-and-run`
and an em dash surrounded by spaces leaves a double hyphen.

### Regenerating diagrams

`docs/architecture/diagrams.md` is the source; the SVGs are generated.

```bash
docker/render-diagrams.sh
```

Semicolons inside a Mermaid message split the statement and cause a parse
error. Use an em dash.

## 7. Design invariants

These are settled and carry through every story. Breaking one is a design
change, not an implementation detail.

1. **Plugins describe, the application renders.** No GL call in plugin code.
   This is what makes the ABI language-agnostic; the prior implementation did
   the opposite and its `plugin.h` is therefore not reusable.
2. **Plain data across the ABI.** No host pointers except pixel buffers, no
   callbacks into the application, nothing unserialisable — so IPC and
   WebAssembly transports stay reachable (R-1).
3. **Exceptions may be used while initialising, never on the frame path**, and
   never across the C ABI in either direction.
4. **One queue for uploads** (R-3), **one resolver for media references**
   (R-13), **the scene list behind a content source** (R-11). Cheap now,
   invasive later.
5. **Per-pixel work belongs in compiled code**, never in an interpreted plugin
   language.
6. **The raw C ABI must stay usable without `libplugin_dev`** — the native
   `image` plugin uses no helper, and that is deliberate (R-14).
