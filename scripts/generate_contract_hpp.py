#!/usr/bin/env python3
"""
Generates the C++ contract header (namespace ``contract``) from
contract/asyncapi.yaml.

The contract is the single source of truth. This generator is fully
data-driven: it never hardcodes schema names, field paths, or enum members.
It discovers two declarative conventions anywhere in the document and emits
their C++ mirrors:

  * ``x-constants``, a map ``{ jsonSchemaKeyword: BaseName }`` attached to any
    schema node. The sibling keyword's value is emitted as
    ``inline constexpr int k<BaseName>``.
        Username: { minLength: 3, x-constants: { minLength: UsernameMin } }
        ->  inline constexpr int kUsernameMin = 3;

  * ``x-enums``, named enums under ``components/x-enums``. Each becomes a C++
    ``enum class <Name>`` with ``k``-prefixed enumerators. Enums whose values
    are strings (wire codes) additionally get a ``k<Name>Str`` map from
    enumerator to wire string.

Adding a new constant or enum is a contract-only edit; this script does not
change. It is the backend counterpart to frontend/scripts/generate-schemas.js.

Usage: generate_contract_hpp.py <asyncapi.yaml> <output.hpp>
"""
import os
import sys

import yaml


def die(msg):
    sys.stderr.write(f"[generate-contract-hpp] error: {msg}\n")
    sys.exit(1)


def walk_constants(node, out):
    """Recursively collect (k-name, int value) from every x-constants map.

    ``out`` is a dict used as an insertion-ordered set (first occurrence wins),
    so callers control emission order by the order they seed the walk.
    """
    if isinstance(node, dict):
        annotations = node.get("x-constants")
        if isinstance(annotations, dict):
            for keyword, base_name in annotations.items():
                if keyword not in node:
                    die(f"x-constants references '{keyword}' but the sibling "
                        f"keyword is absent (constant '{base_name}')")
                out.setdefault("k" + base_name, node[keyword])
        for key, value in node.items():
            if key == "x-constants":
                continue
            walk_constants(value, out)
    elif isinstance(node, list):
        for item in node:
            walk_constants(item, out)


def render_enum(name, definition):
    """Render a single x-enum as C++ lines. Returns (lines, needs_map)."""
    raw_values = definition.get("values") or {}
    if not raw_values:
        die(f"x-enum '{name}' has no values")

    # Normalize: each entry may be a plain scalar or { value: N, display: "..." }.
    values = {
        k: (v["value"] if isinstance(v, dict) else v)
        for k, v in raw_values.items()
    }

    is_string = any(isinstance(v, str) for v in values.values())
    lines = []
    if definition.get("description"):
        lines.append(f"    // {definition['description']}")

    if is_string:
        # String-valued enums (wire codes): sequential enumerators + a
        # enumerator -> wire-string map for (de)serialization.
        lines.append(f"    enum class {name} : uint8_t {{")
        for key in values:
            lines.append(f"        k{key},")
        lines.append("    };")
        lines.append("")
        lines.append(f"    inline const std::unordered_map<{name}, std::string> "
                     f"k{name}Str {{")
        for key, val in values.items():
            lines.append(f'        {{ {name}::k{key}, "{val}" }},')
        lines.append("    };")
    else:
        lines.append(f"    enum class {name} : uint8_t {{")
        for key, val in values.items():
            lines.append(f"        k{key} = {int(val)},")
        lines.append("    };")

    return lines, is_string


def main():
    if len(sys.argv) != 3:
        die("usage: generate_contract_hpp.py <asyncapi.yaml> <output.hpp>")
    src, out = sys.argv[1], sys.argv[2]

    with open(src, "r", encoding="utf-8") as f:
        data = yaml.safe_load(f)

    components = data.get("components") or {}

    # Seed the constant walk schemas-first, then messages, then the rest of the
    # document so emission order is stable and human-friendly.
    consts = {}
    walk_constants(components.get("schemas"), consts)
    walk_constants(components.get("messages"), consts)
    walk_constants(data, consts)

    x_enums = components.get("x-enums") or {}

    needs_string_headers = any(
        any(isinstance(v, str) for v in (d.get("values") or {}).values())
        for d in x_enums.values()
    )

    lines = [
        "#pragma once",
        "",
        "// AUTO-GENERATED, do not edit by hand.",
        "// Source:     contract/asyncapi.yaml (x-constants + x-enums)",
        "// Generator:  scripts/generate_contract_hpp.py (run by CMake target gen_contract_hpp)",
        "//",
        "// Same single source of truth the frontend derives via",
        "// scripts/generate-schemas.js, so C++ and TypeScript never drift.",
        "",
        "#include <cstdint>",
    ]
    if needs_string_headers:
        lines.append("#include <string>")
        lines.append("#include <unordered_map>")
    lines.append("")
    lines.append("namespace contract {")

    if consts:
        width = max(len(name) for name in consts)
        for name, value in consts.items():
            lines.append(f"    inline constexpr int {name.ljust(width)} = {int(value)};")

    for enum_name, definition in x_enums.items():
        lines.append("")
        enum_lines, _ = render_enum(enum_name, definition)
        lines.extend(enum_lines)

    lines.append("}  // namespace contract")
    lines.append("")
    text = "\n".join(lines)

    # Write only when the content actually changes so we don't bump the header's
    # mtime and force a needless recompile of everything that includes it.
    try:
        with open(out, "r", encoding="utf-8") as f:
            if f.read() == text:
                return
    except FileNotFoundError:
        pass

    os.makedirs(os.path.dirname(out) or ".", exist_ok=True)
    with open(out, "w", encoding="utf-8") as f:
        f.write(text)
    print(f"[generate-contract-hpp] Written {out}")


if __name__ == "__main__":
    main()
