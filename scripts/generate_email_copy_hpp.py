#!/usr/bin/env python3
"""
Generates the C++ email-copy header (namespace ``email_copy``) from the
frontend's translated message bundles.

Source of truth: frontend/messages/<locale>.json (inlang message format).
Only keys matching ``^email_`` are pulled in — every other UI string is out
of scope for transactional email. ``pseudo.json`` is a pseudo-localization
bundle used for UI layout testing only, never a real locale, so it is
skipped even though it may be present alongside the real bundles.

Usage: generate_email_copy_hpp.py <messages_dir> <output.hpp>
"""
import json
import os
import re
import sys

EMAIL_KEY_RE = re.compile(r"^email_")
SKIP_FILES = {"pseudo.json"}


def die(msg):
    sys.stderr.write(f"[generate-email-copy-hpp] error: {msg}\n")
    sys.exit(1)


def escape_cpp_string(value, locale, key):
    if "\n" in value or "\r" in value:
        die(f"value for key '{key}' in locale '{locale}' contains a raw "
            f"newline; email copy must be single-line "
            f"(offending value: {value!r})")
    return value.replace("\\", "\\\\").replace('"', '\\"')


def collect_entries(messages_dir):
    """Returns a sorted list of (locale, key, value) triples for every
    email_*-prefixed key across every non-pseudo locale bundle."""
    entries = []
    if not os.path.isdir(messages_dir):
        die(f"messages directory '{messages_dir}' does not exist")

    filenames = sorted(f for f in os.listdir(messages_dir) if f.endswith(".json"))
    if not filenames:
        die(f"no .json files found under '{messages_dir}'")

    for filename in filenames:
        if filename in SKIP_FILES:
            continue
        locale = filename[:-len(".json")]
        path = os.path.join(messages_dir, filename)
        with open(path, "r", encoding="utf-8") as f:
            data = json.load(f)
        if not isinstance(data, dict):
            die(f"'{path}' does not contain a JSON object at the top level")

        for key, value in data.items():
            if not EMAIL_KEY_RE.match(key):
                continue
            if not isinstance(value, str):
                die(f"key '{key}' in locale '{locale}' is not a string "
                    f"(got {type(value).__name__})")
            entries.append((locale, key, value))

    if not entries:
        die(f"no 'email_*' keys found across any locale under '{messages_dir}'")

    entries.sort(key=lambda e: (e[0], e[1]))
    return entries


def render(entries):
    lines = [
        "#pragma once",
        "",
        "// AUTO-GENERATED, do not edit by hand.",
        "// Source:     frontend/messages/<locale>.json ('email_*'-prefixed keys)",
        "// Generator:  scripts/generate_email_copy_hpp.py (run by CMake target gen_email_copy_hpp)",
        "//",
        "// Keeps outbound-email copy in the same translation bundles the",
        "// frontend UI draws from, so backend and frontend strings never drift.",
        "",
        "#include <algorithm>",
        "#include <string>",
        "#include <string_view>",
        "",
        "namespace email_copy {",
        "",
        "struct Entry {",
        "    std::string_view locale;",
        "    std::string_view key;",
        "    std::string_view value;",
        "};",
        "",
        "inline constexpr Entry kEntries[] = {",
    ]
    for locale, key, value in entries:
        escaped = escape_cpp_string(value, locale, key)
        lines.append(f'    {{"{locale}", "{key}", "{escaped}"}},')
    lines.append("};")
    lines.append("")
    lines.append("// Falls back to the \"en\" bundle when the requested locale is")
    lines.append("// unknown, or is known but missing this particular key.")
    lines.append("inline std::string Get(std::string_view key, std::string_view locale) {")
    lines.append("    for (const auto& entry : kEntries) {")
    lines.append("        if (entry.locale == locale && entry.key == key) {")
    lines.append("            return std::string(entry.value);")
    lines.append("        }")
    lines.append("    }")
    lines.append("    for (const auto& entry : kEntries) {")
    lines.append('        if (entry.locale == "en" && entry.key == key) {')
    lines.append("            return std::string(entry.value);")
    lines.append("        }")
    lines.append("    }")
    lines.append("    return std::string();")
    lines.append("}")
    lines.append("")
    lines.append("}  // namespace email_copy")
    lines.append("")
    return "\n".join(lines)


def main():
    if len(sys.argv) != 3:
        die("usage: generate_email_copy_hpp.py <messages_dir> <output.hpp>")
    messages_dir, out = sys.argv[1], sys.argv[2]

    entries = collect_entries(messages_dir)
    text = render(entries)

    # Write only when the content actually changes to avoid bumping the
    # header's mtime and force a needless recompile of everything that
    # includes it.
    try:
        with open(out, "r", encoding="utf-8") as f:
            if f.read() == text:
                return
    except FileNotFoundError:
        pass

    os.makedirs(os.path.dirname(out) or ".", exist_ok=True)
    with open(out, "w", encoding="utf-8") as f:
        f.write(text)
    print(f"[generate-email-copy-hpp] Written {out}")


if __name__ == "__main__":
    main()
