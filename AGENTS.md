
### AGENTS.md

This file is to be used by LLM agents.

Design principles:

- Role: act as senior sw developer
- Coding style: refer to docs/coding-style.md
- SCM: refer to docs/scm.md
- Building: refer to docs/building.md
- Testing: refer to docs/test-plan.md
- Architecture: refer to docs/architecture/ — requirements.md, plugin-api.md,
  diagrams.md. This is *what* the system must do.
- Project plan: refer to docs/project-plan/ — roadmap, status, open decisions,
  risks, prior implementation. This is *when* and *in what order*.
- Phase tags (`[PoC]`, `[P2]`, `[L]`) in the requirements are binding — do not
  implement `[P2]` or `[L]` work as part of a `[PoC]` task.

Building rules:

- Out-of-tree only: configure into `build/`, never in the source tree. When
  building in the container, use `build-docker/` — host and container build
  trees must never be shared.
- No host toolchain? Use the Debian-minimal image in `docker/`; see
  docs/building.md section 5.
- Toolchain: CMake >= 3.20, C++20, CPython >= 3.8 dev files
  (`libpython3-dev` on Debian/Ubuntu). Do not hardcode a Python version;
  `find_package(Python3 ... COMPONENTS Interpreter Development.Embed)` resolves it.
- `<Python.h>` stays confined to `src/python_interpreter.cc`. New code includes
  `src/python_interpreter.h`; anything touching the CPython C API goes behind
  that facade.
- Register new sources in the `python_bridge` target in `CMakeLists.txt`,
  headers listed alongside their `.cc`.
- Verify a change with `cmake --build build` followed by
  `ctest --test-dir build --output-on-failure`. Report build failures rather
  than working around them.

