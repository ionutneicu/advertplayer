### Plugin API

Status: **proposed**. The header is
[`include/refapp/plugin.h`](../../include/refapp/plugin.h); this document is
why it looks the way it does. [Section 8](#8-decisions-needing-sign-off) lists
what it revises in the requirements.

#### 1. The whole contract

A plugin is a shared object exporting **one symbol**, `refapp_plugin_entry`:
identity, a parameter schema, and four function pointers.

```c
typedef struct {
    uint64_t     magic;                                   /* identity     */
    uint32_t     abi_version;
    const char  *name;

    const refapp_param *params;                           /* reflection   */
    uint32_t            param_count;

    refapp_status (*create)      (const refapp_create_info *, void **instance);
    refapp_status (*frame)       (void *instance, const refapp_frame_in *,
                                                  refapp_frame_out *);
    refapp_update_result
                  (*on_content_update)(void *instance,
                                       const refapp_create_info *);
    void          (*update_stats)(void *instance, const refapp_load_stats *);
    void          (*destroy)     (void *instance);
} refapp_plugin;
```

`on_content_update` and `update_stats` may be NULL. `create`, `frame` and
`destroy` are required.

##### Adopting a content update

When the Updater installs new data, each running instance is asked whether it
can carry on or must be recreated:

```c
typedef enum {
    REFAPP_UPDATE_ADOPTED = 0,       /* took the new data, carry on */
    REFAPP_UPDATE_NEEDS_RESTART = 1  /* destroy and create me again */
} refapp_update_result;
```

`info` is a fresh `refapp_create_info` — the same arguments and resolved media
the instance would get if created now — so a plugin able to adopt has
everything it needs without a second mechanism.

**NULL means `NEEDS_RESTART`.** A plugin opts in to live update and never opts
out of it, so a plugin author who has not thought about the case gets the safe
behaviour (FR-118).

The decision sits with the plugin because nothing else knows the answer: a
slideshow can take a new image list between slides, a video decoder cannot
re-seek without a visible break, and neither fact is a property of the file.

> Declared in ABI v1 although the Updater is `[P2]` and the PoC never calls
> it. Adding a function pointer later forces a version bump and invalidates
> every third-party plugin (R-10); adding it now costs one unused field.

Verified in the build container: a complete plugin — background texture,
rotating geometry, a LOAD/KEEP/DROP timeline and a four-field parameter schema
— compiles as C99 under `-Wall -Wextra -Wpedantic -Werror`, the same header
compiles as C++20 host-side, `dlopen`/`dlsym` round-trips, and the object
exports nothing else:

```
$ nm -D --defined-only probe.so
0000000000003dc0 D refapp_plugin_entry
```

Sizes, x86-64:

| Struct | Bytes |
| ------ | ----- |
| `refapp_plugin` | 56 |
| `refapp_create_info` | 56 |
| `refapp_frame_out` | 32 |
| `refapp_resource_op` | 48 |
| `refapp_load_stat` | 16 |
| `refapp_draw_item` | 12 |

#### 2. Call semantics

| Call | When | Blocking |
| ---- | ---- | -------- |
| `create` | Scene start, once per plugin instance | **Synchronous.** The application waits. The instance is fully initialised on return, or `REFAPP_ERROR` was returned. No partial state, no readiness callback. |
| `frame` | Once per frame, render thread, in scene list order | Must return within the frame budget (FR-36, NFR-2). |
| `update_stats` | Between frames, render thread | Returns promptly; the plugin stores the table. |
| `destroy` | Scene end, or after any failure | **Synchronous.** The scene is not torn down until it returns. |

Because `create` is synchronous, expensive setup belongs there rather than in
the first frame, where it would overrun the budget and be skipped.

#### 3. Frame time is the *next* frame

`refapp_frame_in::frame_time_ms` is the scene time at which the frame now being
prepared is intended to be **displayed** — not the time of the frame currently
on screen. A plugin produces content for the moment it will actually be seen.

Everything else that could go in this struct is deliberately absent: wall clock
belongs to the schedule, frame index and delta are derivable, and surface
resolution is withheld outright (FR-13).

#### 4. Residency as a declared timeline

The plugin does not merely react to the current frame — it declares what should
be true of each resource, and by when.

```c
typedef enum { REFAPP_OP_LOAD, REFAPP_OP_KEEP, REFAPP_OP_DROP } refapp_op;
typedef enum { REFAPP_RESOURCE_TEXTURE, REFAPP_RESOURCE_VERTICES,
               REFAPP_RESOURCE_SHADER } refapp_resource_kind;
```

| Op | Meaning of `at_ms` | Carries data |
| -- | ------------------ | ------------ |
| `LOAD` | Deadline: resident **by** this time | Yes |
| `KEEP` | Still needed **at least until** this time | No |
| `DROP` | Not needed **from** this time onward | No |

`KEEP` exists so that eviction under memory pressure (FR-53) has something to
respect. Without it, silence would be ambiguous — the application could not
distinguish "still in use" from "forgotten".

Two consequences worth stating:

- **Geometry is a resource.** Draw items reference `vertices_id`, not an inline
  array. Re-issuing `LOAD` on a live id replaces its contents, which is how
  dynamic geometry works: the clock's hands re-load a six-vertex buffer each
  frame with rotated corners, while the face texture is loaded once and kept.
- **One list replaced three.** `uploads`, `releases` and prefetch hints
  collapse into `ops`, and `refapp_frame_out` is down to two arrays and a flag.

`REFAPP_RESOURCE_SHADER` is reserved: shaders are application-created for now
(FR-50), so `LOAD` for that kind arrives with FR-51.

#### 5. Load cost statistics

The plugin decides *when* to issue a `LOAD`, which means it needs to know what
loading costs. Only the application can know that — it is platform dependent
and only measurable by doing it.

```c
typedef struct {
    refapp_asset_kind kind;                 /* TEXTURE or SHADER */
    union {
        struct { uint32_t max_width, max_height; }        texture;
        struct { uint32_t max_lines; refapp_shader_stage stage; } shader;
    } bound;
    uint32_t load_time_max_ms;
} refapp_load_stat;
```

Entries are **upper bounds**: "an asset within this bound costs at most this
long". A plugin finds the smallest entry covering its asset and issues the
`LOAD` that far ahead of the deadline. For example, seeded defaults of

```
texture <= 300x300     -> 10 ms
texture <= 1000x1000   -> 50 ms
fragment shader <= 10 lines -> 10 ms
```

let a plugin needing a 256×256 texture at scene time 5000 ms issue its `LOAD`
by 4990 ms.

The application seeds the table in `create` from platform defaults, measures
what assets actually cost as the scene runs, and revises through
`update_stats`. It tracks the **maximum** observed, so estimates rise when the
platform proves slower and fall when it proves faster.

This splits the responsibility cleanly: the application knows what things
cost, the plugin knows what it will need and when. Neither can do the other's
half.

#### 6. Reflection

A plugin declares the parameters it accepts, as static data on the descriptor.
The server-side scene editor's tooling `dlopen`s a plugin, reads the schema,
and renders an editing form — drag a plugin into a scene, see its fields, edit
them with the right widget and validation.

```c
typedef struct {
    const char        *name;          /* key in the scene file */
    const char        *label;         /* editor caption; may be NULL */
    refapp_param_type  type;
    bool               required;
    const char        *default_value;
    union { ... }      limits;        /* min/max, lengths, max_lines */
} refapp_param;
```

Types: `STRING`, `TEXT` (multi-line), `INTEGER`, `REAL`, `DATETIME`, `TIME`,
`COLOR_RGB`, `COLOR_RGBA`, `FONT`, `IMAGE`, and `VIDEO` reserved for later.

Values stay strings across the ABI (§8.3), so each type fixes an encoding —
`#RRGGBBAA` for colour, ISO 8601 for datetime, a logical media name for
`IMAGE`, and so on. The full table is in the header.

**Reflection makes the manifest's media list derivable.** Because `IMAGE`
parameters are declared, the editor knows exactly which argument values name
media, and can generate the content set's `media[]` (FR-87) from the schemas
and the values. Nobody maintains that list twice — which removes the one real
cost of the §8.2 decision below.

#### 7. Ownership and lifetime

One rule covers everything:

> Every pointer the plugin puts in `refapp_frame_out` is **borrowed by the
> application until `frame` returns**. The application copies what it needs.
> The plugin retains ownership and may reuse the same buffers next frame.

Consequences:

- A plugin can serve every frame from buffers allocated once in `create`. No
  per-frame allocation is required, and none is expected.
- Pixel and vertex data in a `LOAD` is read during the call and not retained.
- Strings in `refapp_create_info`, and the `refapp_load_stats` table passed to
  `create` or `update_stats`, are borrowed for the duration of that call. A
  plugin that needs them later copies them.
- Resources an instance loaded are released when it is destroyed. `destroy`
  need not emit `DROP` for them.

#### 8. Decisions needing sign-off

##### 8.1 Prefetch became an operation timeline

FR-41 describes three response parts: display set, residency changes, prefetch
hints with a TTS. The header has **one** operation list.

Only the plugin can produce pixels, so a prefetch hint naming an id the
application has never seen is unactionable — it would have to call back into
the plugin, which the no-callbacks rule forbids. "Prefetch" is therefore just
`LOAD` with a future deadline, and with load statistics the plugin can compute
that deadline properly.

> Revises FR-41, FR-42, FR-43. Adds `KEEP` and makes vertices a resource kind.

##### 8.2 Media is resolved by the application, declared in the manifest

FR-24 makes arguments opaque to the application; risk R-13 says plugins never
construct paths. Those conflict: an application that cannot interpret arguments
cannot know which values name media.

Resolution: media is declared in the manifest, the application resolves it, and
`create` receives a name → path table. Arguments stay opaque, paths stay out of
plugins, and content addressing (FR-85) changes only `path`.

The cost — media declared rather than inferred — is now paid by the editor
rather than a human, since reflection (§6) tells it which parameters are
images.

##### 8.3 Argument and parameter values are strings

OP-8 proposed typed values across the ABI. A tagged union costs a discriminant
and per-type validity rules on both sides of every future transport, to save a
`strtod` that `libplugin_dev` does in one line. The reflection schema carries
the type, so the editor still validates properly and the plugin still knows
what to parse.

> Revises OP-8.

##### 8.4 `REAL` was added to the parameter types

The listed types did not include a floating-point number, but the scene
example used `width_percent` with a note that it "could be also float". `REAL`
is included on that basis. Say if it should go.

#### 9. Not yet specified

- **`libplugin_dev`** (FR-88 – FR-93): the helper library's own API. It sits
  above this header and cannot change it. Natural first contents: the load-stat
  lookup, parameter parsing per encoding, and rotated-quad builders.
- **Plugin developer guide** and the C / C++ / Python boilerplate generators.
- **Application-side loading**: how `dlopen` failures, magic mismatches and
  version mismatches are reported. Plugin-invisible.
- **The scene editor itself**, beyond the schema it consumes.
