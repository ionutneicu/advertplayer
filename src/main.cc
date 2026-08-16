#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "python_interpreter.h"

namespace {

// Location of the Python modules shipped with the application. The
// environment variable wins so the binary can be pointed at a working copy
// without a rebuild.
std::string ResolveScriptsDirectory() {
  const char* from_env = std::getenv("REFAPP_SCRIPTS_DIR");
  if (from_env != nullptr && *from_env != '\0') return from_env;
#ifdef REFAPP_SCRIPTS_DIR
  return REFAPP_SCRIPTS_DIR;
#else
  return "scripts";
#endif
}

void PrintSection(const std::string& title) {
  std::cout << "\n== " << title << " ==\n";
}

// Runs statements and reads values straight out of the __main__ namespace.
void DemonstrateInlineCode(refapp::PythonInterpreter& python) {
  PrintSection("Inline code");

  python.Exec(R"(
import math

def gauss_weight(offset, sigma):
    """One tap of a separable Gaussian blur kernel."""
    return math.exp(-(offset * offset) / (2.0 * sigma * sigma))

kernel = [gauss_weight(x, 1.6) for x in range(-2, 3)]
total = sum(kernel)
kernel = [w / total for w in kernel]
)");

  const refapp::PyValue kernel = python.GetGlobal("kernel");
  std::cout << "kernel type : " << kernel.TypeName() << "\n";
  std::cout << "kernel taps : ";
  for (const refapp::PyValue& tap : kernel.ToVector()) {
    std::cout << tap.ToDouble() << ' ';
  }
  std::cout << "\n";

  std::cout << "sum(kernel) : " << python.Eval("sum(kernel)").ToDouble() << "\n";
}

// Pushes C++ values into Python and reads the results back.
void DemonstrateValueExchange(refapp::PythonInterpreter& python) {
  PrintSection("Value exchange");

  python.SetGlobal("viewport_width", refapp::PyValue(std::int64_t{2560}));
  python.SetGlobal("viewport_height", refapp::PyValue(std::int64_t{1440}));
  python.SetGlobal("renderer_name", refapp::PyValue("opengl-refapp"));

  const double aspect =
      python.Eval("viewport_width / viewport_height").ToDouble();
  std::cout << "aspect ratio: " << aspect << "\n";
  // Integers from Python must convert cleanly; ToDouble uses PyNumber_Float.
  std::cout << "frame width : "
            << python.Eval("viewport_width").ToDouble() << "\n";
  std::cout << "banner      : "
            << python.Eval("f'{renderer_name} @ {viewport_width}x{viewport_height}'")
                   .ToString()
            << "\n";
}

// Imports a module from the shipped scripts directory and calls into it.
void DemonstrateModuleCalls(refapp::PythonInterpreter& python) {
  PrintSection("Module calls");

  const refapp::PyValue vertices = refapp::PyValue::MakeList({
      refapp::PyValue(0.0),
      refapp::PyValue(0.5),
      refapp::PyValue(-0.5),
      refapp::PyValue(-0.5),
      refapp::PyValue(0.5),
      refapp::PyValue(-0.5),
  });

  const refapp::PyValue scaled = python.CallFunction(
      "refapp_demo", "scale_vertices", {vertices, refapp::PyValue(2.0)});
  std::cout << "scaled      : " << scaled.Repr() << "\n";

  const refapp::PyValue shader = python.CallFunction(
      "refapp_demo", "build_fragment_shader",
      {refapp::PyValue("tint"), refapp::PyValue::MakeList({
                                    refapp::PyValue("u_time"),
                                    refapp::PyValue("u_resolution"),
                                })});
  std::cout << "generated shader:\n" << shader.ToString() << "\n";

  const refapp::PyValue summary =
      python.CallFunction("refapp_demo", "describe_runtime", {});
  std::cout << "runtime     : " << summary.ToString() << "\n";
}

// Python exceptions surface as C++ exceptions carrying the traceback.
void DemonstrateErrorHandling(refapp::PythonInterpreter& python) {
  PrintSection("Error handling");

  try {
    python.Exec("raise RuntimeError('shader compilation failed')");
    std::cout << "unexpected: no exception was raised\n";
  } catch (const refapp::PythonError& error) {
    std::cout << "caught PythonError:\n" << error.what() << '\n';
  }
}

}  // namespace

int main() {
  try {
    const std::string scripts_directory = ResolveScriptsDirectory();
    refapp::PythonInterpreter python({scripts_directory});

    std::cout << "scripts dir : " << scripts_directory << "\n";
    std::cout << "interpreter : " << python.Version() << "\n";

    DemonstrateInlineCode(python);
    DemonstrateValueExchange(python);
    DemonstrateModuleCalls(python);
    DemonstrateErrorHandling(python);

    std::cout << "\nDone.\n";
    return EXIT_SUCCESS;
  } catch (const std::exception& error) {
    std::cerr << "fatal: " << error.what() << "\n";
    return EXIT_FAILURE;
  }
}
