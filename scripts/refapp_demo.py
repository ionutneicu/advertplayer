"""Python side of the embedding reference application.

The C++ host prepends this directory to ``sys.path`` and imports this module
by name, so nothing here depends on the current working directory.
"""

import platform
import sys

_FRAGMENT_SHADER_TEMPLATE = """#version 330 core

{uniforms}

in vec2 v_uv;
out vec4 frag_colour;

void main() {{
    frag_colour = {name}(v_uv);
}}
"""


def scale_vertices(vertices, factor):
    """Scales a flat sequence of vertex components by ``factor``."""
    return [component * factor for component in vertices]


def build_fragment_shader(name, uniforms):
    """Generates a small GLSL fragment shader stub.

    Args:
      name: Name of the colouring function called from ``main``.
      uniforms: Names of the uniforms to declare, in order.

    Returns:
      The shader source as a single string.
    """
    declarations = "\n".join(f"uniform float {uniform};" for uniform in uniforms)
    return _FRAGMENT_SHADER_TEMPLATE.format(uniforms=declarations, name=name)


def describe_runtime():
    """Returns a one-line description of the embedded interpreter."""
    return (
        f"{platform.python_implementation()} {platform.python_version()} "
        f"on {sys.platform}"
    )
