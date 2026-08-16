/*
 * opengl-refapp plugin ABI.
 *
 * This header is the entire contract between the application and a plugin.
 * A plugin is a shared object exporting exactly one symbol, `refapp_plugin`.
 * Nothing else is required of it, and nothing here depends on the language the
 * plugin is written in (requirements FR-32, FR-34).
 *
 * Design rules, in force for every addition to this file:
 *
 *   1. Plain data only. No host pointers other than pixel buffers, no
 *      callbacks from plugin into application, no shared ownership. Everything
 *      crossing the boundary must be serialisable, so a future transport
 *      (IPC, WebAssembly) can carry it unchanged (FR-21, FR-38, risk R-1).
 *   2. The application never calls into a plugin except through the three
 *      function pointers below.
 *   3. A plugin never constructs a filesystem path. The application resolves
 *      media and hands over resolved entries (FR-61, FR-85, risk R-13).
 *
 * C99 or later; also valid C++.
 */

#ifndef REFAPP_PLUGIN_H_
#define REFAPP_PLUGIN_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * Identity
 * ------------------------------------------------------------------------- */

/* Confirms a loaded object really is a plugin. */
#define REFAPP_PLUGIN_MAGIC 0x5245464150500001ULL

/* Bumped on any incompatible change below. The application rejects a plugin
 * whose version it does not recognise, rather than crashing on a struct it
 * cannot interpret. */
#define REFAPP_ABI_VERSION 1u

typedef enum {
    REFAPP_OK = 0,
    REFAPP_ERROR = 1
} refapp_status;

/* Answer to "the content changed underneath you". */
typedef enum {
    /* The instance has taken the new data and can carry on. */
    REFAPP_UPDATE_ADOPTED = 0,

    /* The instance cannot adopt in place; destroy and create it again. */
    REFAPP_UPDATE_NEEDS_RESTART = 1
} refapp_update_result;

/* -------------------------------------------------------------------------
 * Well-known ids
 * ------------------------------------------------------------------------- */

/* Shaders created by the application (FR-50). Plugin-supplied shaders are a
 * later phase; until then these are the only legal shader ids. */
#define REFAPP_SHADER_IDENTITY 0u

/* The only accepted pixel format (FR-45, FR-47). RGBA, 8 bits per channel,
 * alpha PREMULTIPLIED. Cairo's ARGB32 is premultiplied already but is
 * native-endian, so on little-endian hosts its byte order must be swizzled
 * before upload -- libplugin_dev does this (FR-91). */
#define REFAPP_PIXEL_RGBA8_PREMUL 0u

/* -------------------------------------------------------------------------
 * Construction
 * ------------------------------------------------------------------------- */

/* One configuration argument, verbatim from the scene file (FR-24).
 *
 * Values are strings even when the scene file wrote a number or boolean: it
 * keeps this struct trivially serialisable, and typed accessors belong in
 * libplugin_dev where they cost the ABI nothing. */
typedef struct {
    const char *key;
    const char *value;
} refapp_arg;

/* One media file the application has already located on the plugin's behalf.
 *
 * `name` is the logical name as written in the scene arguments, e.g.
 * "midnight.jpg". `path` is where it actually is. The indirection is what
 * later allows content-addressed storage without any plugin changing. */
typedef struct {
    const char *name;
    const char *path;
} refapp_media;

/* -------------------------------------------------------------------------
 * Load cost statistics
 *
 * How long the application takes to make an asset resident, as a function of
 * its size. Platform dependent -- the same texture costs different amounts on
 * different GPUs -- so the application measures it rather than anyone
 * guessing.
 *
 * A plugin uses this table to work out how far ahead of `needed_at_ms` it must
 * offer something. That decision belongs to the plugin: only it knows what it
 * will draw and when.
 *
 * Entries are upper bounds, read as "an asset within this bound costs at most
 * this long". Find the smallest entry that covers the asset.
 * ------------------------------------------------------------------------- */

typedef enum {
    REFAPP_ASSET_TEXTURE = 0,
    REFAPP_ASSET_SHADER = 1
} refapp_asset_kind;

typedef enum {
    REFAPP_SHADER_STAGE_VERTEX = 0,
    REFAPP_SHADER_STAGE_FRAGMENT = 1
} refapp_shader_stage;

typedef struct {
    refapp_asset_kind kind;

    union {
        /* Applies to textures up to max_width x max_height. */
        struct {
            uint32_t max_width;
            uint32_t max_height;
        } texture;

        /* Applies to shaders of this stage up to max_lines long. */
        struct {
            uint32_t max_lines;
            refapp_shader_stage stage;
        } shader;
    } bound;

    /* Worst case observed, or the seeded default before anything was
     * measured. Milliseconds. */
    uint32_t load_time_max_ms;
} refapp_load_stat;

typedef struct {
    const refapp_load_stat *items;
    uint32_t count;
} refapp_load_stats;

typedef struct {
    const char *instance_name; /* for diagnostics; unique within a scene */

    const refapp_arg *args;
    uint32_t arg_count;

    const refapp_media *media;
    uint32_t media_count;

    /* Seed values, from the application's defaults for this platform or from
     * what it has measured so far. Revised later through update_stats. */
    refapp_load_stats stats;
} refapp_create_info;

/* -------------------------------------------------------------------------
 * Per-frame exchange
 * ------------------------------------------------------------------------- */

typedef struct {
    /* Scene time at which the frame now being prepared is intended to be
     * displayed -- the *next* frame, not the one on screen. Milliseconds since
     * the scene started (FR-40); deliberately not wall clock.
     *
     * Deliberately the only field: a plugin is never told the surface
     * resolution and must not need it (FR-13). */
    uint64_t frame_time_ms;
} refapp_frame_in;

/* One vertex: position in normalised device coordinates, [-1, 1], and the
 * texture coordinate to sample at it. */
typedef struct {
    float x, y;
    float u, v;
} refapp_vertex;

/* One draw request: geometry, texture and shader, all referenced by id.
 *
 * Everything it names must be resident, which the plugin arranges through the
 * operations below. There are no transform matrices anywhere in this ABI --
 * rotation and scaling live in the vertex data (FR-63). */
typedef struct {
    uint32_t vertices_id; /* plugin-scoped */
    uint32_t texture_id;  /* plugin-scoped */
    uint32_t shader_id;   /* REFAPP_SHADER_IDENTITY unless told otherwise */
} refapp_draw_item;

/* -------------------------------------------------------------------------
 * Residency operations
 *
 * The plugin does not merely react to the current frame; it declares a
 * timeline. Each operation says what should be true of one resource, and by
 * when. The application schedules the work so it lands before the deadline
 * without disturbing the frame rate (FR-48).
 *
 * Ids are chosen by the plugin and scoped to the instance: two instances may
 * both use id 3 for different things.
 * ------------------------------------------------------------------------- */

typedef enum {
    /* Make resident by `at_ms`. Carries the data. Re-loading a live id
     * replaces its contents -- this is how dynamic geometry works. */
    REFAPP_OP_LOAD = 0,

    /* Still needed at least until `at_ms`. Protects against eviction; without
     * it the application is free to reclaim under memory pressure. */
    REFAPP_OP_KEEP = 1,

    /* Not needed from `at_ms` onward. The application may free it. */
    REFAPP_OP_DROP = 2
} refapp_op;

typedef enum {
    REFAPP_RESOURCE_TEXTURE = 0,
    REFAPP_RESOURCE_VERTICES = 1,

    /* Reserved. Shaders are application-created for now (FR-50); LOAD for
     * this kind arrives with plugin-supplied shaders (FR-51). KEEP and DROP
     * are meaningless until then. */
    REFAPP_RESOURCE_SHADER = 2
} refapp_resource_kind;

typedef struct {
    refapp_op op;
    refapp_resource_kind kind;
    uint32_t id;

    /* Scene time this operation refers to. Its meaning follows the op: the
     * deadline for LOAD, the lower bound for KEEP, the release point for
     * DROP. Zero means "now, this frame".
     *
     * For LOAD the plugin works out how early to emit the operation, using
     * refapp_load_stats to estimate the cost. Emitting too late means the
     * deadline is missed and draw items naming the resource are skipped. */
    uint64_t at_ms;

    /* Populated for LOAD only; ignored for KEEP and DROP. */
    union {
        struct {
            uint32_t width;
            uint32_t height;
            uint32_t format;    /* REFAPP_PIXEL_RGBA8_PREMUL */
            const void *pixels; /* width * height * 4 bytes */
        } texture;

        struct {
            const refapp_vertex *vertices;
            uint32_t count; /* multiple of three; drawn as GL_TRIANGLES */
        } vertices;
    } data;
} refapp_resource_op;

typedef struct {
    /* What to draw, bottom to top within this instance. May be empty: an
     * instance that is not visible this frame still schedules operations
     * (FR-19). */
    const refapp_draw_item *draw_items;
    uint32_t draw_item_count;

    /* The resource timeline: what to have ready, keep, and release. May
     * describe frames well beyond this one. */
    const refapp_resource_op *ops;
    uint32_t op_count;

    /* This instance has nothing further to contribute. Feeds the scene's
     * "all" and "any" end conditions (FR-17, FR-44). */
    bool finished;
} refapp_frame_out;

/* -------------------------------------------------------------------------
 * Reflection
 *
 * A plugin declares the parameters it accepts, so the server-side scene editor
 * can present them: drag a plugin in, see its fields, edit them with the right
 * widget and validation, and write the result into the scene file.
 *
 * This is static data on the plugin descriptor, not a call: the editor's
 * tooling can dlopen a plugin and read it without creating an instance.
 *
 * Values remain strings across the ABI. Each type fixes an encoding:
 *
 *   STRING, TEXT   verbatim; TEXT may contain newlines
 *   INTEGER        decimal, optional leading '-'
 *   REAL           decimal with '.' as separator, C locale
 *   DATETIME       ISO 8601, "YYYY-MM-DDTHH:MM:SS"
 *   TIME           "HH:MM:SS"
 *   COLOR_RGB      "#RRGGBB"
 *   COLOR_RGBA     "#RRGGBBAA"
 *   FONT           family name
 *   IMAGE          logical media name, resolved through refapp_media
 *
 * Because IMAGE parameters are declared, the media a scene needs is derivable
 * from its plugin schemas and argument values -- the editor generates the
 * manifest's media list rather than an author maintaining it twice.
 * ------------------------------------------------------------------------- */

typedef enum {
    REFAPP_PARAM_STRING = 0,
    REFAPP_PARAM_TEXT = 1, /* multi-line */
    REFAPP_PARAM_INTEGER = 2,
    REFAPP_PARAM_REAL = 3,
    REFAPP_PARAM_DATETIME = 4,
    REFAPP_PARAM_TIME = 5,
    REFAPP_PARAM_COLOR_RGB = 6,
    REFAPP_PARAM_COLOR_RGBA = 7,
    REFAPP_PARAM_FONT = 8,
    REFAPP_PARAM_IMAGE = 9,

    /* Reserved; not in the PoC. */
    REFAPP_PARAM_VIDEO = 10
} refapp_param_type;

typedef struct {
    /* Key as it appears in the scene file's arguments. */
    const char *name;

    /* Human-readable name for the editor. May be NULL, in which case the
     * editor shows `name`. */
    const char *label;

    refapp_param_type type;

    /* Whether the editor must insist on a value. */
    bool required;

    /* Encoded as above, or NULL when there is no default. */
    const char *default_value;

    /* Editor-side validation. The member that applies follows `type`; leave
     * the rest zeroed. A zero bound means "no bound". */
    union {
        struct {
            int64_t min;
            int64_t max;
        } integer;

        struct {
            double min;
            double max;
        } real;

        /* STRING, TEXT and the string-encoded types. */
        struct {
            uint32_t min_length;
            uint32_t max_length;
            uint32_t max_lines; /* TEXT only */
        } text;
    } limits;
} refapp_param;

/* -------------------------------------------------------------------------
 * The plugin
 * ------------------------------------------------------------------------- */

typedef struct {
    uint64_t magic;       /* REFAPP_PLUGIN_MAGIC */
    uint32_t abi_version; /* REFAPP_ABI_VERSION */
    const char *name;     /* plugin type name; also the .so basename */

    /* Parameters this plugin accepts. Read by the scene editor's tooling
     * without creating an instance; the application itself only passes
     * arguments through (FR-24). May be empty. */
    const refapp_param *params;
    uint32_t param_count;

    /* Create one instance. Called at scene start, once per entry in the
     * scene's plugin list. `*instance` is opaque to the application and is
     * handed back to every later call.
     *
     * SYNCHRONOUS: the application waits for this to return before proceeding.
     * The instance is fully initialised, or REFAPP_ERROR was returned. There
     * is no partially-created state and no readiness callback. Long work
     * belongs here rather than in the first frame, where it would blow the
     * budget. */
    refapp_status (*create)(const refapp_create_info *info, void **instance);

    /* Produce this frame's contribution.
     *
     * Called on the render thread, once per frame, in scene list order. Must
     * return within the frame budget: a plugin that marshals into another
     * language or talks to another process does that work elsewhere and has
     * the result ready here (FR-36).
     *
     * Every pointer in `out` is borrowed by the application only until this
     * call returns; it copies whatever it needs. The plugin keeps ownership
     * and may reuse the same buffers next frame. */
    refapp_status (*frame)(void *instance,
                           const refapp_frame_in *in,
                           refapp_frame_out *out);

    /* The content this instance was created from has been updated. Return
     * whether it can carry on with the new data, or must be recreated.
     *
     * Optional -- may be NULL, which means REFAPP_UPDATE_NEEDS_RESTART. A
     * plugin opts in to live update; it never opts out (FR-118).
     *
     * `info` is a fresh refapp_create_info: the same arguments and resolved
     * media the instance would receive if it were created now. On
     * REFAPP_UPDATE_ADOPTED the instance must have taken everything it needs
     * from it before returning, since it is borrowed for the call only.
     *
     * Called between frames, on the render thread. Not called in the PoC --
     * the Updater is a later phase (FR-111, FR-112) -- but declared now so
     * that adding it later does not force an ABI version bump. */
    refapp_update_result (*on_content_update)(void *instance,
                                              const refapp_create_info *info);

    /* Revise the load cost table. Optional -- may be NULL.
     *
     * The application measures how long assets actually take to become
     * resident and corrects its estimates as a scene runs. A plugin that
     * schedules ahead should use the latest table; one that does not can leave
     * this NULL and keep whatever it was given at create.
     *
     * Called between frames, on the render thread. `stats` is borrowed for the
     * duration of the call. */
    void (*update_stats)(void *instance, const refapp_load_stats *stats);

    /* Destroy an instance. Called at scene end, and on failure of any call
     * above.
     *
     * SYNCHRONOUS: the application waits for this to return before the scene
     * is considered torn down. Resources the instance loaded are released by
     * the application; the plugin need not emit DROP operations for them. */
    void (*destroy)(void *instance);
} refapp_plugin;

/* The one symbol a plugin exports. */
extern const refapp_plugin refapp_plugin_entry;

#define REFAPP_PLUGIN_SYMBOL "refapp_plugin_entry"

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* REFAPP_PLUGIN_H_ */
