// <Python.h> must precede every other include: it defines feature test macros
// that change how the standard library headers are compiled.
#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "python_interpreter.h"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace refapp {
namespace {

constexpr char kProgramName[] = "opengl-refapp";

// Copies a Python str into a std::string. Returns an empty string and leaves
// the Python error indicator set when |object| is not decodable.
std::string ToUtf8(PyObject* object) {
  Py_ssize_t size = 0;
  const char* data = PyUnicode_AsUTF8AndSize(object, &size);
  if (data == nullptr) return {};
  return std::string(data, static_cast<std::size_t>(size));
}

// Formats the pending Python exception, traceback included, and clears the
// error indicator. Returns an empty string when nothing is raised.
std::string FetchErrorMessage() {
  if (PyErr_Occurred() == nullptr) return {};

#if PY_VERSION_HEX >= 0x030C0000
  PyObject* exception = PyErr_GetRaisedException();
#else
  PyObject* type = nullptr;
  PyObject* exception = nullptr;
  PyObject* traceback = nullptr;
  PyErr_Fetch(&type, &exception, &traceback);
  PyErr_NormalizeException(&type, &exception, &traceback);
  if (exception != nullptr && traceback != nullptr) {
    PyException_SetTraceback(exception, traceback);
  }
  Py_XDECREF(type);
  Py_XDECREF(traceback);
#endif
  if (exception == nullptr) return "unknown Python error";

  std::string message;
  PyObject* traceback_module = PyImport_ImportModule("traceback");
  if (traceback_module != nullptr) {
#if PY_VERSION_HEX >= 0x030A0000
    // Since 3.10 format_exception() takes the exception alone and reads the
    // traceback off it.
    PyObject* lines =
        PyObject_CallMethod(traceback_module, "format_exception", "O", exception);
#else
    PyObject* tb = PyObject_GetAttrString(exception, "__traceback__");
    if (tb == nullptr) {
      PyErr_Clear();
      Py_INCREF(Py_None);
      tb = Py_None;
    }
    PyObject* lines = PyObject_CallMethod(
        traceback_module, "format_exception", "OOO",
        reinterpret_cast<PyObject*>(Py_TYPE(exception)), exception, tb);
    Py_DECREF(tb);
#endif
    if (lines != nullptr) {
      PyObject* separator = PyUnicode_FromString("");
      if (separator != nullptr) {
        PyObject* joined = PyUnicode_Join(separator, lines);
        if (joined != nullptr) {
          message = ToUtf8(joined);
          Py_DECREF(joined);
        }
        Py_DECREF(separator);
      }
      Py_DECREF(lines);
    }
    Py_DECREF(traceback_module);
  }

  // The traceback module is unavailable during interpreter shutdown; fall
  // back to the exception's own string form.
  if (message.empty()) {
    PyObject* text = PyObject_Str(exception);
    if (text != nullptr) {
      message = ToUtf8(text);
      Py_DECREF(text);
    }
  }

  Py_DECREF(exception);
  PyErr_Clear();  // Discard anything raised by the formatting itself.
  return message.empty() ? "unknown Python error" : message;
}

// Throws PythonError with the pending exception appended when |ok| is false.
void ThrowOnError(bool ok, std::string_view context) {
  if (ok) return;
  const std::string detail = FetchErrorMessage();
  std::string message(context);
  if (!detail.empty()) {
    message += ":\n";
    message += detail;
  }
  throw PythonError(message);
}

// Converts a PyStatus failure into a PythonError. The Python error indicator
// is not usable at this point, so the status carries the only diagnostic.
[[noreturn]] void ThrowStatus(const PyStatus& status, std::string_view context) {
  std::string message(context);
  if (status.err_msg != nullptr) {
    message += ": ";
    message += status.err_msg;
  }
  throw PythonError(message);
}

PyObject* NewUnicode(std::string_view text) {
  PyObject* value = PyUnicode_FromStringAndSize(
      text.data(), static_cast<Py_ssize_t>(text.size()));
  ThrowOnError(value != nullptr, "failed to create a Python string");
  return value;
}

// Inserts |directory| at the front of sys.path.
void PrependSysPath(const std::string& directory) {
  PyObject* sys = PyImport_ImportModule("sys");
  ThrowOnError(sys != nullptr, "failed to import sys");
  PyObject* search_path = PyObject_GetAttrString(sys, "path");
  Py_DECREF(sys);
  ThrowOnError(search_path != nullptr, "failed to read sys.path");

  PyObject* entry = PyUnicode_FromStringAndSize(
      directory.data(), static_cast<Py_ssize_t>(directory.size()));
  if (entry == nullptr) {
    Py_DECREF(search_path);
    ThrowOnError(false, "failed to encode a sys.path entry");
  }
  const int inserted = PyList_Insert(search_path, 0, entry);
  Py_DECREF(entry);
  Py_DECREF(search_path);
  ThrowOnError(inserted == 0, "failed to extend sys.path with " + directory);
}

}  // namespace

// ---------------------------------------------------------------------------
// PyValue
// ---------------------------------------------------------------------------

PyValue::PyValue() {
  Py_INCREF(Py_None);
  object_ = Py_None;
}

PyValue::PyValue(bool value) {
  object_ = PyBool_FromLong(value ? 1 : 0);
  ThrowOnError(object_ != nullptr, "failed to create a Python bool");
}

PyValue::PyValue(std::int64_t value) {
  object_ = PyLong_FromLongLong(static_cast<long long>(value));
  ThrowOnError(object_ != nullptr, "failed to create a Python int");
}

PyValue::PyValue(double value) {
  object_ = PyFloat_FromDouble(value);
  ThrowOnError(object_ != nullptr, "failed to create a Python float");
}

PyValue::PyValue(std::string_view value) : object_(NewUnicode(value)) {}

PyValue::PyValue(const char* value)
    : object_(NewUnicode(value == nullptr ? std::string_view("")
                                          : std::string_view(value))) {}

PyValue::PyValue(const PyValue& other) : object_(other.object_) {
  Py_XINCREF(object_);
}

PyValue& PyValue::operator=(const PyValue& other) {
  if (this != &other) {
    PyObject* previous = object_;
    object_ = other.object_;
    Py_XINCREF(object_);
    Py_XDECREF(previous);
  }
  return *this;
}

PyValue::PyValue(PyValue&& other) noexcept
    : object_(std::exchange(other.object_, nullptr)) {}

PyValue& PyValue::operator=(PyValue&& other) noexcept {
  if (this != &other) {
    Py_XDECREF(object_);
    object_ = std::exchange(other.object_, nullptr);
  }
  return *this;
}

PyValue::~PyValue() { Py_XDECREF(object_); }

PyValue PyValue::Steal(PyObject* object) {
  PyValue value;
  Py_XDECREF(value.object_);
  value.object_ = object;
  return value;
}

PyValue PyValue::Borrow(PyObject* object) {
  Py_XINCREF(object);
  return Steal(object);
}

PyValue PyValue::MakeList(const std::vector<PyValue>& items) {
  PyObject* list = PyList_New(static_cast<Py_ssize_t>(items.size()));
  ThrowOnError(list != nullptr, "failed to create a Python list");
  for (std::size_t i = 0; i < items.size(); ++i) {
    PyObject* item = items[i].get();
    Py_XINCREF(item);
    // Safe on a freshly allocated list: no slot has been filled yet.
    PyList_SET_ITEM(list, static_cast<Py_ssize_t>(i), item);
  }
  return Steal(list);
}

bool PyValue::is_none() const { return object_ == nullptr || object_ == Py_None; }

std::string PyValue::TypeName() const {
  if (object_ == nullptr) return "null";
  return Py_TYPE(object_)->tp_name;
}

std::string PyValue::ToString() const {
  PyObject* text = PyObject_Str(object_);
  ThrowOnError(text != nullptr, "str() failed on a Python value");
  std::string result = ToUtf8(text);
  Py_DECREF(text);
  return result;
}

std::string PyValue::Repr() const {
  PyObject* text = PyObject_Repr(object_);
  ThrowOnError(text != nullptr, "repr() failed on a Python value");
  std::string result = ToUtf8(text);
  Py_DECREF(text);
  return result;
}

std::int64_t PyValue::ToInt() const {
  PyObject* number = PyNumber_Long(object_);
  ThrowOnError(number != nullptr, "value is not convertible to int");
  const long long result = PyLong_AsLongLong(number);
  Py_DECREF(number);
  ThrowOnError(result != -1 || PyErr_Occurred() == nullptr,
               "int value does not fit in 64 bits");
  return static_cast<std::int64_t>(result);
}

double PyValue::ToDouble() const {
  PyObject* number = PyNumber_Float(object_);
  ThrowOnError(number != nullptr, "value is not convertible to float");
  const double result = PyFloat_AsDouble(number);
  Py_DECREF(number);
  ThrowOnError(result != -1.0 || PyErr_Occurred() == nullptr,
               "float value out of range");
  return result;
}

bool PyValue::ToBool() const {
  const int result = PyObject_IsTrue(object_);
  ThrowOnError(result != -1, "value has no truth value");
  return result == 1;
}

std::vector<PyValue> PyValue::ToVector() const {
  PyObject* iterator = PyObject_GetIter(object_);
  ThrowOnError(iterator != nullptr, "value is not iterable");

  std::vector<PyValue> items;
  while (PyObject* item = PyIter_Next(iterator)) {
    items.push_back(Steal(item));
  }
  Py_DECREF(iterator);
  ThrowOnError(PyErr_Occurred() == nullptr, "iteration failed");
  return items;
}

PyValue PyValue::GetAttr(std::string_view name) const {
  PyObject* key = NewUnicode(name);
  PyObject* attribute = PyObject_GetAttr(object_, key);
  Py_DECREF(key);
  ThrowOnError(attribute != nullptr,
               "no attribute '" + std::string(name) + "'");
  return Steal(attribute);
}

PyValue PyValue::Call(const std::vector<PyValue>& args) const {
  PyObject* argument_tuple = PyTuple_New(static_cast<Py_ssize_t>(args.size()));
  ThrowOnError(argument_tuple != nullptr, "failed to build an argument tuple");
  for (std::size_t i = 0; i < args.size(); ++i) {
    PyObject* item = args[i].get();
    Py_XINCREF(item);
    PyTuple_SET_ITEM(argument_tuple, static_cast<Py_ssize_t>(i), item);
  }

  PyObject* result = PyObject_CallObject(object_, argument_tuple);
  Py_DECREF(argument_tuple);
  ThrowOnError(result != nullptr, "Python call raised");
  return Steal(result);
}

// ---------------------------------------------------------------------------
// PythonInterpreter
// ---------------------------------------------------------------------------

PythonInterpreter::PythonInterpreter(
    const std::vector<std::string>& module_search_paths) {
  if (Py_IsInitialized()) {
    throw PythonError("a Python interpreter is already running in this process");
  }

  PyConfig config;
  PyConfig_InitPythonConfig(&config);
  // The host owns argv and its own signal handling; Python must not claim
  // either of them.
  config.install_signal_handlers = 0;

  PyStatus status =
      PyConfig_SetBytesString(&config, &config.program_name, kProgramName);
  if (PyStatus_Exception(status)) {
    PyConfig_Clear(&config);
    ThrowStatus(status, "failed to configure the interpreter");
  }

  status = Py_InitializeFromConfig(&config);
  PyConfig_Clear(&config);
  if (PyStatus_Exception(status)) {
    ThrowStatus(status, "failed to initialise the interpreter");
  }

  try {
    PyObject* main_module = PyImport_AddModule("__main__");  // Borrowed.
    ThrowOnError(main_module != nullptr, "failed to reach the __main__ module");
    main_globals_ = PyModule_GetDict(main_module);  // Borrowed.
    ThrowOnError(main_globals_ != nullptr, "failed to reach __main__ globals");

    // Inserting in reverse keeps the caller's ordering at the front of
    // sys.path.
    for (auto it = module_search_paths.rbegin();
         it != module_search_paths.rend(); ++it) {
      PrependSysPath(*it);
    }
  } catch (...) {
    main_globals_ = nullptr;
    Py_FinalizeEx();
    throw;
  }
}

PythonInterpreter::~PythonInterpreter() {
  main_globals_ = nullptr;
  if (Py_IsInitialized()) {
    Py_FinalizeEx();
  }
}

void PythonInterpreter::Exec(std::string_view code) {
  const std::string source(code);
  PyObject* result = PyRun_String(source.c_str(), Py_file_input, main_globals_,
                                  main_globals_);
  ThrowOnError(result != nullptr, "failed to execute Python statements");
  Py_DECREF(result);
}

PyValue PythonInterpreter::Eval(std::string_view expression) {
  const std::string source(expression);
  PyObject* result = PyRun_String(source.c_str(), Py_eval_input, main_globals_,
                                  main_globals_);
  ThrowOnError(result != nullptr,
               "failed to evaluate '" + source + "'");
  return PyValue::Steal(result);
}

PyValue PythonInterpreter::Import(std::string_view module_name) {
  const std::string name(module_name);
  PyObject* module = PyImport_ImportModule(name.c_str());
  ThrowOnError(module != nullptr, "failed to import module '" + name + "'");
  return PyValue::Steal(module);
}

PyValue PythonInterpreter::CallFunction(std::string_view module_name,
                                        std::string_view function_name,
                                        const std::vector<PyValue>& args) {
  return Import(module_name).GetAttr(function_name).Call(args);
}

PyValue PythonInterpreter::GetGlobal(std::string_view name) {
  PyObject* key = NewUnicode(name);
  PyObject* value = PyDict_GetItemWithError(main_globals_, key);  // Borrowed.
  Py_DECREF(key);
  if (value == nullptr) {
    ThrowOnError(PyErr_Occurred() == nullptr, "failed to read a global");
    throw PythonError("no such global: '" + std::string(name) + "'");
  }
  return PyValue::Borrow(value);
}

void PythonInterpreter::SetGlobal(std::string_view name, const PyValue& value) {
  PyObject* key = NewUnicode(name);
  const int stored = PyDict_SetItem(main_globals_, key, value.get());
  Py_DECREF(key);
  ThrowOnError(stored == 0, "failed to set global '" + std::string(name) + "'");
}

std::string PythonInterpreter::Version() {
  return Import("sys").GetAttr("version").ToString();
}

}  // namespace refapp
