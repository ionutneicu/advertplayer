### Roadmap and delivery phases

How the work is sequenced and what each phase tag in
[requirements](../architecture/requirements.md) means. The requirements say
*what*; this says *when*.

Every requirement carries a phase tag. **The `[PoC]` tag is the contract for
what gets built first** — everything else exists in this document only so the
PoC does not paint itself into a corner.

| Tag | Meaning |
| --- | ------- |
| **`[PoC]`** | In the proof of concept. Must work end to end. |
| **`[P2]`** | Next phase. Not built now, but the PoC must not preclude it. |
| **`[L]`** | Long term / needs research. Direction only. |

#### Delivery roadmap

| Step | Deliverable | Phase |
| ---- | ----------- | ----- |
| **1** | **Renderer** on X11, Wayland and Raspberry Pi / DispmanX. Resources created by hand, no server. Reference plugins: background image / slideshow, analog clock, scrolling marquee. | **`[PoC]`** |
| 2 | Updater, client and server side, plus the data layout it moves | `[P2]` |
| 3 | Web interface, without plugin preview | `[P2]` |
| 4 | Android client; plugin previews in the web interface | `[L]` |
| 5 | Portable plugin drawing across platforms — unifying Cairo with Canvas and similar | `[L]` |

**Only step 1 is in scope.** Everything beyond it is recorded so the PoC does
not preclude it, not as work to start.

#### Mid-term goals

Not requirements yet, but direction that should shape decisions taken now.

| Goal | Why |
| ---- | --- |
| **Client uniformity** | A fleet of many client variants is a cost paid forever, by whoever operates the server. Every capability difference multiplies the content that must be prepared, tested and stored. Prefer a small number of client profiles over accommodating whatever hardware appears — and prefer changing the fleet to teaching the server about it. |

This is why FR-170 puts resolution-dependent work on the server, and why
FR-168 derives profiles rather than enumerating devices: both reduce the number
of distinct things the server has to reason about.

#### PoC investigations

Open points that cannot be settled by deciding, only by measuring. Each is
scheduled inside the PoC rather than blocking its start, and each has a
provisional value to build against — see
[open-decisions.md](open-decisions.md#b--resolve-during-the-poc).

| Investigation | Answers | When |
| ------------- | ------- | ---- |
| Port the prior DispmanX backend to a device | OP-22, OP-23, OP-24 | **Early.** It is the task most likely to fail in a way that changes the plan. |
| Measure frame rate and plugin budget on the slowest target | OP-11, NFR-1, NFR-2 | Once one scene renders |
| Build the device assessment: texture ladder + shader compile, fitted to fixed-cost-plus-per-byte | FR-194, FR-198 – FR-200, and seeds FR-95 | Alongside the resource manager — it is the only honest source of the load-stat defaults |
| Look at the Cairo clock and marquee on a real panel | OP-33 | Once those plugins exist |
| Inventory available test hardware | OP-25 | Any time |
