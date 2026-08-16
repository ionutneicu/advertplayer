### Architecture diagrams

Companion to [requirements.md](requirements.md) and
[plugin-api.md](plugin-api.md). Diagrams are Mermaid, so they render inline on
GitHub and GitLab and stay diffable in review.

Phase tags follow the requirements: **`[PoC]`**, `[P2]`, `[L]`.

---

#### 1. System overview

Everything below the dashed line is the application. Plugins sit outside it and
are reached only through the ABI.

```mermaid
flowchart TB
    subgraph ext["External — [P2] / [L]"]
        SRV["Content server"]
        SYNC["Sync service<br/><i>FR-31</i>"]
    end

    subgraph store["Local content store"]
        SCN["scenes/<br/>scene list + scene files"]
        PLG["plugins/<br/>image.so, clock.so"]
        STO["storage/<br/>flat media blobs<br/><i>FR-61</i>"]
    end

    subgraph app["Application"]
        CSRC["Content source<br/><i>FR-60, risk R-11</i>"]
        PLAY["Scene player<br/><i>FR-16..FR-20</i>"]
        LOAD["Plugin loader<br/>dlopen + magic check<br/><i>FR-25, FR-33</i>"]
        RES["Resource manager<br/>textures, shaders, residency<br/><i>FR-46..FR-52</i>"]
        REND["Renderer — GLES2<br/><i>FR-10..FR-15</i>"]
        SURF["Surface layer — EGL<br/><i>FR-5..FR-7</i>"]
    end

    subgraph plugins["Plugins — outside the application"]
        PI["Plugin instances<br/><i>refapp_plugin</i>"]
        LPD["libplugin_dev<br/>static, optional<br/><i>FR-88..FR-91</i>"]
    end

    X11["X11"]
    WL["Wayland"]

    SRV --> SYNC
    SYNC --> SCN
    SYNC --> PLG
    SYNC --> STO

    SCN --> CSRC
    STO --> CSRC
    CSRC --> PLAY
    PLG --> LOAD
    PLAY --> LOAD
    LOAD --> PI
    PLAY <-->|"create / frame / destroy"| PI
    LPD -.->|"linked into"| PI
    PLAY --> RES
    PLAY --> REND
    RES --> REND
    REND --> SURF
    SURF --> X11
    SURF --> WL
```

<sub>Rendered: [01-system-overview.svg](diagrams/01-system-overview.svg)</sub>

---

#### 2. Platform layering

One GL path serves both display servers; EGL is the seam that makes that true
and keeps a WebGL target reachable.

```mermaid
flowchart TB
    APP["Application render loop"]
    GLES["OpenGL ES 2.0<br/><i>FR-10</i>"]
    EGL["EGL<br/><i>FR-5 — never GLX</i>"]
    BX["X11 backend<br/><i>ENABLE_X11</i>"]
    BW["Wayland backend<br/><i>ENABLE_WAYLAND</i>"]
    MESA["Mesa — radeonsi / iris / v3d"]
    VEND["Vendor driver — NVIDIA<br/><i>DOC-1</i>"]
    WEB["WebGL<br/><i>[L] — FR-3</i>"]

    APP --> GLES
    GLES --> EGL
    EGL --> BX
    EGL --> BW
    BX --> MESA
    BW --> MESA
    BX --> VEND
    BW --> VEND
    GLES -.->|"future target"| WEB

    style WEB stroke-dasharray: 4 4
```

<sub>Rendered: [02-platform-layering.svg](diagrams/02-platform-layering.svg)</sub>

---

#### 3. Runtime data model

Ownership, not classes. Matches §7 of the requirements.

```mermaid
erDiagram
    CONTENT_SET ||--|| SCENE_LIST : "declares"
    CONTENT_SET ||--o{ MEDIA : "declares"
    SCENE_LIST  ||--o{ SCENE : "ordered play order"
    SCENE       ||--o{ PLUGIN_INSTANCE : "ordered — bottom to top"
    PLUGIN_INSTANCE }o--|| PLUGIN_TYPE : "resolves to plugins/TYPE.so"
    PLUGIN_INSTANCE ||--o{ ARGUMENT : "opaque to the application"
    ARGUMENT    }o--o| MEDIA : "references by logical name"

    CONTENT_SET {
        string version "immutable, FR-80"
        string activation "timestamp or immediate"
        string expires "optional"
    }
    SCENE {
        string name
        string end_condition "duration all any never"
    }
    PLUGIN_INSTANCE {
        string instance_name
        string type
    }
    ARGUMENT {
        string key
        string value "always a string, api 6.3"
    }
    MEDIA {
        string logical_name
        string content_address "FR-85"
        int size
    }
```

<sub>Rendered: [03-runtime-data-model.svg](diagrams/03-runtime-data-model.svg)</sub>

---

#### 4. Plugin layering

Why the C shim exists twice over: it is the ABI boundary, and it is where
pixel loops must live whatever the plugin's logic is written in.

```mermaid
flowchart LR
    subgraph appside["Application"]
        A["Plugin loader<br/>+ scene player"]
    end

    subgraph native["Native plugin"]
        NC["C or C++ logic"]
    end

    subgraph py["Python plugin"]
        PC["C shim<br/><i>embeds CPython</i>"]
        PD["libplugin_dev<br/><i>pixel conversion, FR-91</i>"]
        PY["Python logic<br/><i>Cairo clock, FR-66</i>"]
    end

    A -->|"refapp_plugin ABI"| NC
    A -->|"refapp_plugin ABI"| PC
    PC <-->|"marshalling"| PY
    PC --- PD
    PY -.->|"pixels via Cairo"| PC

    note1["FR-90: per-pixel work never crosses<br/>into the interpreted layer"]
    PD -.- note1

    style note1 fill:transparent,stroke-dasharray: 3 3
```

<sub>Rendered: [04-plugin-layering.svg](diagrams/04-plugin-layering.svg)</sub>

---

#### 5. Sequence — scene start

```mermaid
sequenceDiagram
    autonumber
    participant CS as Content source
    participant P as Scene player
    participant L as Plugin loader
    participant PI as Plugin instance

    P->>CS: resolve scene "midnight"
    CS-->>P: end condition, ordered plugin list
    CS-->>P: resolved media table (name -> path)

    loop each plugin entry, in declared order
        P->>L: load type "image"
        L->>L: dlopen plugins/image.so
        L->>L: dlsym "refapp_plugin_entry"
        L->>L: verify magic + abi_version
        alt rejected
            L-->>P: error
            P->>P: log, skip to next scene (FR-58, FR-59)
        else accepted
            L-->>P: const refapp_plugin *
            Note over P: schema in refapp_plugin.params<br/>is static: no instance needed
            P->>PI: create(instance_name, args, media, load stats)
            Note over PI: synchronous — fully<br/>initialised on return
            PI-->>P: void *instance
        end
    end

    P->>P: scene_time_ms = 0, enter frame loop
```

<sub>Rendered: [05-sequence-scene-start.svg](diagrams/05-sequence-scene-start.svg)</sub>

---

#### 6. Sequence — one frame

The hot path. Note that nothing here allocates: plugins serve from buffers
owned since `create`, and the application copies what it keeps.

```mermaid
sequenceDiagram
    autonumber
    participant P as Scene player
    participant PI as Plugin instances
    participant RM as Resource manager
    participant R as Renderer

    R->>R: begin frame, clear

    loop each instance, list order (bottom to top)
        P->>PI: frame(instance, frame_time_ms of NEXT frame)
        activate PI
        Note over PI: must return within<br/>frame budget (FR-36, NFR-2)
        PI-->>P: draw_items, ops, finished
        deactivate PI

        alt overran budget
            P->>P: log, skip this instance
        end

        P->>RM: apply ops — LOAD by at_ms,<br/>KEEP until at_ms, DROP from at_ms
    end

    RM->>RM: drain LOAD queue by deadline,<br/>within remaining budget (FR-48)
    RM->>RM: measure actual load times
    RM-->>PI: update_stats(revised table)
    P->>R: submit draw items, in plugin order
    R->>R: blend GL_ONE, GL_ONE_MINUS_SRC_ALPHA (FR-15)
    R->>R: swap buffers, vsync

    alt all / any instance finished
        P->>P: end scene (FR-17)
    end
```

<sub>Rendered: [06-sequence-one-frame.svg](diagrams/06-sequence-one-frame.svg)</sub>

---

#### 7. Sequence — content update and activation `[P2]`

```mermaid
sequenceDiagram
    autonumber
    participant SRV as Server
    participant SY as Sync service
    participant ST as Staging
    participant P as Scene player

    SRV->>SY: content set v18, activation = T
    SY->>SY: resolve transitive media set
    SY->>SY: diff against storage (FR-69)
    SY->>ST: download only the difference

    Note over ST: incomplete — invisible<br/>to the player (FR-82)

    ST-->>SY: complete + validated
    SY->>SY: mark staged (pinned, FR-84)

    alt scheduled (stage 2)
        Note over P: v17 keeps playing
        P->>P: current scene ends naturally
        SY->>P: activate v18 at scene boundary (FR-86)
        Note over P: no glitch
    else immediate (stage 3)
        SY->>P: activate v18 now
        P->>P: cut current scene
        Note over P: brief glitch, accepted (FR-74)
    end

    P->>P: destroy instances, restart on v18
    SY->>SY: v17 superseded — retire once unreferenced (FR-76, FR-77)
```

<sub>Rendered: [07-sequence-content-update-and-activation-p2.svg](diagrams/07-sequence-content-update-and-activation-p2.svg)</sub>

---

#### 8. State — content set `[P2]`

The pinning rules of FR-84. A set is collectable only from `Retired`.

```mermaid
stateDiagram-v2
    [*] --> Downloading : announced by server
    Downloading --> Staged : complete + validated (FR-73)
    Downloading --> [*] : download failed, discard

    Staged --> Active : activation (FR-83, FR-86)
    Active --> Superseded : newer set activated
    Superseded --> Retired : last running scene torn down
    Staged --> Retired : expired before activating

    Retired --> [*] : media reference count reaches zero (FR-76)

    note right of Staged
        pinned — never collected
    end note
    note right of Superseded
        still pinned: a running scene
        may still be reading it
    end note
```

<sub>Rendered: [08-state-content-set-p2.svg](diagrams/08-state-content-set-p2.svg)</sub>

---

#### 9. State — scene

```mermaid
stateDiagram-v2
    [*] --> Loading
    Loading --> Starting : config parsed, media resolved
    Loading --> Failed : scene file bad / media missing (FR-75)

    Starting --> Playing : all instances created
    Starting --> Failed : an instance failed create (FR-58)

    Playing --> Ending : duration elapsed
    Playing --> Ending : all instances finished
    Playing --> Ending : any instance finished
    Playing --> Ending : activation cut (stage 3)
    Playing --> Failed : instance error or budget overrun

    Failed --> Ending : log, move on (FR-59)
    Ending --> [*] : instances destroyed, GL objects freed

    note right of Playing
        end condition chosen per scene:
        duration / all / any / never (FR-17)
    end note
```

<sub>Rendered: [09-state-scene.svg](diagrams/09-state-scene.svg)</sub>

---

#### 10. State — plugin instance

Mirrors the three ABI entry points exactly; there are no other transitions.

```mermaid
stateDiagram-v2
    [*] --> Created : create(info, &instance)
    Created --> Active : first frame() call
    Created --> [*] : create returned REFAPP_ERROR

    Active --> Active : frame() — REFAPP_OK
    Active --> Active : update_stats()
    Active --> Idle : frame() with empty draw set
    Idle --> Idle : update_stats()
    Idle --> Active : frame() with draw items

    Active --> Finished : out.finished = true
    Idle --> Finished : out.finished = true

    Active --> Faulted : REFAPP_ERROR or budget overrun
    Idle --> Faulted : REFAPP_ERROR or budget overrun

    Finished --> [*] : destroy(instance)
    Faulted --> [*] : destroy(instance)

    note right of Idle
        not visible, but may still
        upload and release (FR-19)
    end note
```

<sub>Rendered: [10-state-plugin-instance.svg](diagrams/10-state-plugin-instance.svg)</sub>

---

#### 11. State — resource residency

Per `(instance, kind, id)`, for textures and vertex buffers alike. The `at_ms`
deadline on a LOAD is what lets `Queued` exist at all — it is the window the
scheduler works in, and the plugin sizes it from the load statistics.

```mermaid
stateDiagram-v2
    [*] --> Queued : LOAD op, deadline at_ms
    Queued --> Resident : uploaded to GPU
    Queued --> Queued : deferred — deadline not near

    Resident --> Queued : LOAD on a live id (replace contents)
    Resident --> Resident : KEEP — pinned until at_ms
    Resident --> Released : DROP from at_ms
    Resident --> Released : owning instance destroyed

    Queued --> Released : owning instance destroyed
    Released --> [*]

    note right of Queued
        deadline missed?
        skip the draw item, log once (OP-9)
    end note
    note right of Resident
        no KEEP and no DROP: eligible
        for eviction under pressure (FR-53)
    end note
```

<sub>Rendered: [11-state-resource-residency.svg](diagrams/11-state-resource-residency.svg)</sub>
