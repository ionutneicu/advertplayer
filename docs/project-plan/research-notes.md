### Research notes

Problems that are understood well enough to write down but not well enough to
turn into requirements. Nothing here is committed to; the point is to keep the
thinking rather than rediscover it.

---

#### 1. CPU cost of offscreen rendering

**The question.** How long does a plugin take to *generate* a texture with
Cairo? The texture ladder (FR-198) models GPU upload; nothing models the CPU
work that produces the pixels in the first place. It matters because the clock
and the marquee (FR-114) both rasterise on the client.

##### Why the texture ladder does not transfer

The hope that cost scales with texture size holds for the GPU and not for the
CPU:

| | Cost scales with |
| - | ---------------- |
| **GPU upload** | Bytes. A 4K texture costs 4× a 1080p one, near enough, whatever is in it. |
| **Cairo rasterisation** | What is drawn. Roughly: path setup per edge, plus fill cost over *covered* pixels, plus per-glyph cost for text, plus a much larger constant for gradients and filters. |

A 4K surface containing one small circle costs almost nothing to rasterise. A
1080p surface of dense antialiased text costs a great deal. **Surface size is
an upper bound on Cairo cost, not a predictor of it** — which is exactly the
asymmetry that makes this hard.

##### Direction A — separate the device factor from the content factor

The most promising decomposition, and the one that reuses what already exists:

```
cost  ≈  content_complexity  ×  device_factor
```

- **`device_factor`** is measured once, by assessment (FR-194), with a *fixed
  synthetic scene* — some paths, some glyphs, some gradient, at a known size.
  It says "this box is 3.2× slower at Cairo than that box". Same shape as the
  texture ladder, different axis.
- **`content_complexity`** is per plugin and per configuration, and is **not**
  measured by assessment — it is observed in the field (Direction B).

The value of the split is that the device half generalises across all plugins
and all content, so it is worth measuring properly once; and the content half
is where the irreducible unpredictability lives.

##### Direction B — the host measures, the plugin does not assess itself

Self-assessment by the plugin is awkward: it would have to sweep its own
lifespan, and a plugin drawing one-shot content has nothing to sweep.

But **the application already times the plugin.** The frame budget check
(FR-36) is a measurement of exactly this. The host can build a per-instance
distribution of "time to produce" without the plugin cooperating at all, and
without a plugin author having to think about it. That data is already flowing
to the server under FR-184.

This is probably the strongest single idea here: *the thing that needs
measuring is already being measured for another reason.*

##### Direction C — budget instead of predict

Invert the problem. Rather than predicting how long a draw will take, tell the
plugin how long it may take, and let it adapt:

- draw fewer optional elements
- disable antialiasing (`CAIRO_ANTIALIAS_NONE`)
- rasterise at a smaller surface and let the GPU scale
- reuse the previous frame's texture if nothing material changed

No prediction is required and it degrades gracefully on hardware nobody
anticipated. It needs plugins to be written to adapt, which is a real cost, and
it overlaps with progressive degradation (FR-189).

##### Direction D — get it off the critical path entirely

Much of the difficulty comes from treating this as a per-frame problem. It
mostly is not:

- the clock face is drawn **once**; only the hands move, and they move as
  geometry over an unchanged texture (FR-63)
- marquee text is drawn **once**; scrolling is geometry
- a slideshow's images are decoded **ahead of** `needed_at_ms`

So the real question may be "what does *scene start* cost?" rather than "what
does a frame cost". If Cairo work happens on a worker thread ahead of its
deadline, prediction stops being needed: the question becomes *did it finish in
time*, which is trivially observable, and the failure is already handled by
skipping the draw item (OP-9).

##### Where this probably lands

D first, because it removes most of the problem rather than modelling it. Then
B, because the measurement is free. Then A, if a design-time predictor
(FR-186) is actually wanted for CPU as well as GPU. C only if real hardware
turns out to need it.

##### To verify before relying on any of this

- That Cairo's image-backend cost really does track covered area and edge
  count. Stated from general knowledge of scanline rasterisers, not measured
  here.
- Whether Cairo's glyph cache makes repeat text effectively free, which would
  make the marquee much cheaper than a first-render measurement suggests.
- Whether a synthetic scene for `device_factor` correlates with real plugin
  content well enough to be worth measuring at all. If it does not, Direction A
  is a dead end and B is the whole answer.

---

#### 2. Streaming content — the marquee

**Why it is the important example.** The clock is a *bounded* problem: the face
and hands are drawn once and reused forever, so Direction D of note 1 disposes
of it, and in practice the hands could just as well be images fetched from the
server. The marquee is not bounded. Long text cannot become one wide texture —
it would exceed `gl.max_texture_size` and fail to upload at all — so the
content must be produced, uploaded, shown and discarded in pieces, continuously,
for as long as the scene runs.

That makes it the plugin that exercises nearly everything: `max_texture_size`,
deadline-driven upload, `KEEP`/`DROP`, memory limits, CPU rasterisation cost,
and per-vertex UV for partial visibility.

##### The shape of the solution

Neither extreme works:

| Approach | Fails because |
| -------- | ------------- |
| One texture for the whole string | Exceeds the maximum texture size |
| Rasterise every tile up front into Cairo surfaces | Exhausts host RAM; the string may be arbitrarily long |
| Spool tiles to disk | Writes on the playback path, and the storage is not the Renderer's to write (FR-133) |

So: draw a tile with Cairo shortly before it is needed, upload it, draw it,
drop it once it has left the screen. A sliding window of a few tiles, sized to
whatever memory allows.

##### Does the current design support it?

Checked against the ABI as it stands. **No hard blocker.**

| Need | Provided by | |
| ---- | ----------- | - |
| Upload a tile with a deadline | `REFAPP_OP_LOAD` with `at_ms` | ✓ |
| Hold the working set | `REFAPP_OP_KEEP` | ✓ |
| Discard a spent tile | `REFAPP_OP_DROP` with `at_ms` | ✓ |
| Choose a tile width that will upload | `gl.max_texture_size` (FR-157) | ✓ |
| Show part of a tile as it slides in | Per-vertex UV (FR-63) | ✓ |
| Rasterise off the frame path | Plugins may use their own threads (FR-57) | ✓ |
| Know how far ahead to start | Load statistics (FR-94 – FR-98) | **partial** |
| Know how many tiles it may hold | — | **gap** |

##### The gap: no memory budget

A plugin has no idea how much memory it may use. `gl.memory_mb` and the host
memory property say what the *machine* has, not what *this instance* may take —
and a scene has several instances, each of which could reason the same way and
keep everything resident.

The consequences are worse than waste. If the application evicts a texture to
recover memory (FR-53), the plugin is not told; its next draw item references a
resource that is gone, and by OP-9 the item is silently skipped. The marquee
does not fail loudly — it develops gaps.

**Direction: budget, do not evict.** Publish a per-instance allowance as a
property, and let plugins size their own working set to it. A plugin that keeps
inside its budget is never evicted, so the failure mode disappears rather than
being handled. Eviction stays as a backstop for plugins that misbehave, not as
the primary mechanism. This is [FR-205](../architecture/requirements.md).

##### The other gap: lead time needs CPU cost, and only GPU cost is modelled

To start tile *n* in time the plugin needs `cairo_draw_time + upload_time`.
Load statistics give the second. The first is note 1, unsolved — so a plugin
computing its lead time today can only guess at half the sum.

For the marquee this is less alarming than it sounds: tiles are uniform, so a
plugin can measure its own first few tiles and use that. It is Direction B of
note 1, applied by the plugin to itself, and it works here precisely because
the content repeats.

##### A hazard worth writing down

Rasterising on a worker thread while `frame` returns borrowed pointers
(plugin-api §7) is a race waiting to happen: the application reads the buffer
until `frame` returns, and a worker must not be writing it then. Plugins that
stream **must** double-buffer. It will not show up in testing on a fast
machine — only on a slow one, intermittently.

##### What is genuinely unknown

The memory-versus-CPU trade-off — few large tiles, or many small ones — cannot
be reasoned out in advance, and the answer differs per device: a machine with
plenty of RAM and a slow CPU wants large tiles drawn rarely; the opposite
machine wants the opposite. This needs measurement on real hardware, and is a
strong argument for the per-instance budget being a *property* the plugin
adapts to, rather than a constant anyone picks.

---

#### 3. Apportioning memory between plugin instances

**The question.** FR-205 gives each instance a texture-memory allowance. What
should the allowance be?

**Why the obvious answer is wrong.** Equal shares fails immediately: a
background image holding one static texture and a marquee streaming tiles do
not want the same allowance, and a scene of five instances does not want a
fifth of memory each when four of them are idle.

**Why it is genuinely hard.** Every instance can legitimately use all available
memory at some moments and almost none at others. Conflicts therefore arise
only at *specific times* — when two instances happen to peak together — and
those coincidences depend on scene timing, on content, and on how fast the
device loads. A static budget cannot express "you may have 200 MB, but not
between 12.4 s and 13.1 s".

##### Direction — a planning sweep

The most promising idea: before playing, ask each instance **what it will need
and when**, without asking it to produce anything.

A plugin walks its own timeline — the same walk it does to schedule loads — and
returns the resource operations it *would* emit: sizes, ids, and `at_ms`, with
no pixel data. Summing those across instances gives a memory-over-time curve
for the whole scene, and the peak is what has to fit.

This is attractive because:

- it needs no new plugin logic; a plugin that can schedule loads can already
  enumerate them
- it is cheap — no rasterising, no uploading
- it composes: the same sweep run on the server (FR-186) predicts at authoring
  time; run on the client it apportions at scene start
- it turns a scheduling problem into a data one

**Costs and unknowns:**

- It is another ABI entry point, and one that only pays off if plugins can
  answer it honestly. A plugin whose content depends on run-time data — a
  marquee whose text arrives from a feed — can only estimate.
- **Compressed textures break it.** The memory a compressed texture occupies
  is not derivable from its dimensions; it depends on the format and,
  for some formats, the content. A size-only sweep would have to assume
  uncompressed, and over-estimate.
- Whether a plugin can predict its own timeline far enough ahead to be useful
  is untested.

##### Status

**Unresolved, and deliberately so.** The PoC needs *some* apportionment to run
three plugins; it does not need the right one. Whatever it uses should be
behind the same property (FR-205) so the policy can change without touching
plugins.
