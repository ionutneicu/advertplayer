### Coding style

The [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html)
applies in full. This document records only where the project adds to it or
makes a choice the guide leaves open.

#### 1. Adoptions

##### File extensions

| Application kind | C++ headers | C headers | Sources |
| ---------------- | ----------- | --------- | ------- |
| Mixed C and C++ | `.hpp` | `.h` | `.cc` / `.c` |
| C++ only | `.h` | — | `.cc` |

This project is **mixed**: the plugin ABI in `include/refapp/` is C, the
application is C++. A C++ header is therefore `.hpp` and a C header is `.h`.

#### 2. Error handling

Google's guide bans exceptions outright. This project departs from that, but
only on one side of a line: **exceptions are allowed while initialising, and
forbidden on the frame path.**

##### Which path am I on?

| Path | Exceptions | What it covers |
| ---- | ---------- | -------------- |
| **Initialisation** | **Allowed** | Application startup, EGL and GL context creation, scene load, plugin load, and everything a plugin does inside `create`. Runs once, latency does not matter, and RAII plus throwing constructors is the clearest way to build a half-constructed-object-free graph. |
| **Frame and periodic** | **Forbidden** | `frame`, `update_stats`, upload draining, the render loop, and anything else called per frame or on a timer. |

The frame path is excluded for a concrete reason, not a stylistic one: it has
a budget (FR-36, NFR-2), and the cost of unwinding is unbounded and not
something a plugin can be held to. A missed budget means the instance is
skipped, so an exception there converts a recoverable error into a dropped
frame.

##### Rules

1. **On the frame path, do not throw.** Return a value describing success or
   failure, in the shape of `std::expected`. The project provides
   `refapp::expected<T, E>` until the standard one is available — see below.
2. **Mark frame-path functions `noexcept`.** It is the compiler-checked form of
   rule 1. If one genuinely cannot be, that is a design problem to fix, not a
   `noexcept` to remove.
3. **Mark every fallible function `[[nodiscard]]`**, on either path. A silently
   ignored error is the failure mode this style exists to prevent.
4. **Never let an exception cross the plugin ABI — on either path.**
   `include/refapp/plugin.h` is C, and unwinding through it is undefined
   behaviour. This is not relaxed for `create`: a plugin may throw freely
   *inside* its initialisation, but it catches before returning and reports
   `REFAPP_ERROR`.
5. **Constructors may throw, if the type is only ever built during
   initialisation.** A type that is constructed on the frame path gets a
   static factory returning `expected` and a private constructor that cannot
   fail.
6. **Programming errors are not runtime errors.** A violated precondition is a
   bug: assert and let it abort. Neither exceptions nor `expected` are for
   those.
7. **`std::bad_alloc` is not handled.** Out of memory is fatal. Wrapping every
   allocation would obscure the code for a case the application cannot recover
   from anyway.

##### The type

`std::expected` is C++23; this project builds as C++20. Use
`refapp::expected<T, E>` — a subset with the same names and semantics:

```cpp
template <typename T, typename E>
class expected {
 public:
  bool has_value() const;
  explicit operator bool() const;

  T& value();              // precondition: has_value()
  const E& error() const;  // precondition: !has_value()
  T value_or(T fallback) const;
};

template <typename E>
class unexpected;          // constructs the error case
```

When the project moves to C++23 this becomes an alias and no call site
changes:

```cpp
namespace refapp {
template <typename T, typename E = Error>
using expected = std::expected<T, E>;
}
```

That migration is the whole reason to mirror the standard names exactly rather
than invent `Result`, `Outcome` or `StatusOr`.

##### Shape

Initialisation — a throwing constructor is fine, and clearer:

```cpp
// Built once, during scene start. Rule 5 permits this.
class EglSurface {
 public:
  // Throws SurfaceError if the display or config cannot be obtained.
  EglSurface(NativeDisplay display, const SurfaceOptions& options);
  ~EglSurface();
};
```

Frame path — a fallible factory, never a throwing constructor:

```cpp
class TextureUpload {
 public:
  [[nodiscard]] static refapp::expected<TextureUpload, Error> Stage(
      const refapp_resource_op& op) noexcept;

 private:
  TextureUpload() = default;  // cannot fail
};

// Call site: check, never ignore.
auto upload = TextureUpload::Stage(op);
if (!upload) {
  LOG(ERROR) << "upload rejected: " << upload.error();
  return refapp::unexpected(upload.error());
}
```

##### At the C boundary

The ABI reports `refapp_status`. Convert at the edge, in both directions, so
neither side sees the other's convention:

```cpp
// Application side: a C status becomes an expected.
[[nodiscard]] refapp::expected<void, Error> CallFrame(
    const refapp_plugin& plugin, void* instance,
    const refapp_frame_in& in, refapp_frame_out& out);

// Plugin side, create: exceptions are allowed inside, but must not escape.
extern "C" refapp_status create(const refapp_create_info* info,
                                void** instance) {
  try {
    *instance = new Impl(*info);  // may throw; that is fine here
    return REFAPP_OK;
  } catch (const std::exception& e) {
    LOG(ERROR) << "create failed: " << e.what();
    return REFAPP_ERROR;
  } catch (...) {
    return REFAPP_ERROR;
  }
}

// Plugin side, frame: nothing should throw in the first place. The handler is
// a backstop against undefined behaviour, not an error strategy.
extern "C" refapp_status frame(void* instance, const refapp_frame_in* in,
                               refapp_frame_out* out) noexcept {
  return Impl::From(instance).Frame(*in, *out) ? REFAPP_OK : REFAPP_ERROR;
}
```

Note the difference. In `create` the handler is the error strategy: it turns a
thrown failure into a status. In `frame` the function is `noexcept` and
nothing below it throws by rule 1 — if something did, terminating is the
correct outcome, because unwinding through the C ABI is undefined behaviour
and a silently skipped frame would hide the bug.

#### 3. Known deviations to fix

- **`src/python_interpreter.h`** is a C++ header in a now-mixed project and
  should be `.hpp` (section 1).

  Its throwing constructor is **conformant** under rule 5: a
  `PythonInterpreter` is built once during initialisation. What it will need
  when it moves into the Python plugin boilerplate (FR-64) is rule 4 — a
  `catch` in the `extern "C" create` wrapper — and rule 1 applied to whatever
  it does per frame, which today is `Exec` and `Eval` throwing `PythonError`.
