### Software configuration management

GitFlow, with `develop` as the trunk.

#### 1. Branch model

| Branch | Cut from | Merges into | Lifetime |
| ------ | -------- | ----------- | -------- |
| `develop` | — | — | Permanent. The integration branch and the trunk of this project. |
| `feature/<name>` | `develop` | `develop`, via merge request | Deleted after merge |
| `release/<version>` | `develop` | — (cherry-picked back) | Kept for the release |
| `hotfix/<name>` | `release/<version>` | That release branch, then cherry-picked to `develop` | Deleted after merge |

```
develop ──┬──────────────────────────┬────────────────────────►
          │                          │              ▲
          └── feature/<name> ──MR────┘              │ cherry-pick
                                                    │
develop ──────────┬── release/<version> ────────────┴────────►
                  │           │
                  │           └── hotfix/<name> ──MR──┘
```

Rules that hold everywhere:

- **No direct pushes to `develop`.** Everything arrives through a merge
  request.
- **Never force-push a shared branch.** Force-push is acceptable only on your
  own unmerged `feature/*` branch, to tidy history before review.
- **One concern per branch.** If a branch grows a second purpose, split it.

#### 2. Branch naming

```
feature/plugin-loader
feature/egl-wayland-backend
release/0.2.0
hotfix/texture-leak-on-scene-end
```

Lowercase, hyphen-separated. No ticket numbers in the branch name — those
belong in the merge request and in a commit trailer.

#### 3. Feature workflow

Start from an up-to-date trunk:

```bash
git switch develop && git pull --ff-only
```

```bash
git switch -c feature/plugin-loader
```

Keep the branch current by **rebasing**, not merging, so history stays linear
and the merge request diff shows only your change:

```bash
git fetch origin && git rebase origin/develop
```

Verify the way CI will, before opening the request:

```bash
cmake --build build-docker -j"$(nproc)" && ctest --test-dir build-docker --output-on-failure
```

#### 4. Merge request

A merge request states:

1. **What changes and why** — not a restatement of the diff.
2. **Which requirements it implements**, by identifier: `FR-25`, `NFR-2`,
   `OP-12`. These come from
   [docs/architecture/requirements.md](architecture/requirements.md) and are
   what ties a change back to a decision.
3. **How it was verified** — which commands were run, on which platform, and
   what came out. "Builds clean" is not verification; the `ctest` output is.
4. **Anything deliberately left out**, and why.

Acceptance criteria:

- Build and full test suite pass.
- At least one review approval.
- No unresolved review threads.
- Requirements touched by the change are updated **in the same merge
  request** — code and specification must not drift apart between requests.

Merging:

- **Squash** when the intermediate commits are noise ("wip", "fix typo"). The
  squashed message follows section 6.
- **Merge `--no-ff`** when each commit is meaningful on its own and worth
  keeping.
- Delete the source branch on merge.

#### 5. Release and hotfix

Cut a release branch from `develop` once the scope is complete:

```bash
git switch develop && git pull --ff-only && git switch -c release/0.2.0
```

From that point the release branch takes stabilisation fixes only. New
features continue on `develop` and are not part of this release.

Fixes made on a release branch are **cherry-picked back to `develop`** — the
release branch is never merged into the trunk:

```bash
git switch develop && git cherry-pick -x <commit>
```

`-x` records the source commit in the message, keeping the two copies
traceable to one another.

Tag the release on its branch:

```bash
git tag -a v0.2.0 -m "Release 0.2.0" && git push origin v0.2.0
```

A hotfix branches from the release branch, merges back into it through a merge
request, and is cherry-picked to `develop` the same way.

#### 6. Commit messages

```
<area>: <imperative summary, 72 chars or fewer>

Why the change is needed, and anything a reader could not infer from the
diff. Wrapped at 72 columns.

Implements: FR-25, FR-33
Refs: #123
```

`<area>` is the part of the tree affected: `loader`, `renderer`, `egl`,
`plugin-api`, `docs`, `docker`.

- Imperative mood: "add plugin loader", not "added" or "adds".
- Explain **why**, not what — the diff already says what.
- Use an `Implements:` trailer with requirement identifiers, so a requirement
  can be traced to the commits that satisfied it.
- A commit that only reformats or renames says so, and contains nothing else.
