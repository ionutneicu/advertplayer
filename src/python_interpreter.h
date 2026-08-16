#ifndef REFAPP_SRC_PYTHON_INTERPRETER_H_
#define REFAPP_SRC_PYTHON_INTERPRETER_H_

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

// PyObject is a typedef for "struct _object". Forward declaring it keeps
// <Python.h> - and its habit of redefining feature test macros - out of every
// translation unit that only needs to talk to the interpreter.
extern "C" {
struct _object;
}

namespace refapp {

// Reports a failure raised by the embedded interpreter. The message carries
// the formatted Python exception, traceback included.
class PythonError : public std::runtime_error {
 public:
  explicit PythonError(const std::string& message)
      : std::runtime_error(message) {}
};

// Owns a strong reference to a single Python object.
//
// Every operation requires a live PythonInterpreter; a PyValue must not
// outlive the interpreter that produced it.
class PyValue {
 public:
  PyValue();  // None
  explicit PyValue(bool value);
  explicit PyValue(std::int64_t value);
  explicit PyValue(double value);
  explicit PyValue(std::string_view value);
  // Without this overload a string literal would decay to const char* and
  // then convert to bool, silently producing a Python bool.
  explicit PyValue(const char* value);

  PyValue(const PyValue& other);
  PyValue& operator=(const PyValue& other);
  PyValue(PyValue&& other) noexcept;
  PyValue& operator=(PyValue&& other) noexcept;
  ~PyValue();

  // Adopts an existing strong reference without incrementing the refcount.
  static PyValue Steal(_object* object);
  // Takes a new strong reference to a borrowed pointer.
  static PyValue Borrow(_object* object);
  static PyValue MakeList(const std::vector<PyValue>& items);

  _object* get() const { return object_; }
  bool is_none() const;

  // Name of the Python type, e.g. "int" or "list".
  std::string TypeName() const;

  std::string ToString() const;  // str(value)
  std::string Repr() const;      // repr(value)
  std::int64_t ToInt() const;
  double ToDouble() const;
  bool ToBool() const;
  // Drains any Python iterable into a vector.
  std::vector<PyValue> ToVector() const;

  PyValue GetAttr(std::string_view name) const;
  PyValue Call(const std::vector<PyValue>& args) const;

 private:
  _object* object_ = nullptr;
};

// RAII owner of the embedded CPython runtime.
//
// At most one instance may be alive per process: construction initialises the
// interpreter, destruction finalises it. All calls must happen on the thread
// that constructed the object, which holds the GIL for its lifetime.
class PythonInterpreter {
 public:
  // |module_search_paths| are prepended to sys.path, in the given order, so
  // the host can ship its own Python modules.
  explicit PythonInterpreter(
      const std::vector<std::string>& module_search_paths = {});
  ~PythonInterpreter();

  PythonInterpreter(const PythonInterpreter&) = delete;
  PythonInterpreter& operator=(const PythonInterpreter&) = delete;

  // Executes |code| as a sequence of statements in the __main__ namespace.
  void Exec(std::string_view code);

  // Evaluates a single |expression| in the __main__ namespace.
  PyValue Eval(std::string_view expression);

  PyValue Import(std::string_view module_name);

  // Calls module_name.function_name(*args).
  PyValue CallFunction(std::string_view module_name,
                       std::string_view function_name,
                       const std::vector<PyValue>& args);

  PyValue GetGlobal(std::string_view name);
  void SetGlobal(std::string_view name, const PyValue& value);

  // Full sys.version banner of the embedded runtime.
  std::string Version();

 private:
  // Borrowed reference to the __main__ module dictionary, owned by the
  // interpreter itself.
  _object* main_globals_ = nullptr;
};

}  // namespace refapp

#endif  // REFAPP_SRC_PYTHON_INTERPRETER_H_
