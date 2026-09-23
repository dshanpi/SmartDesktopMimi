#!/usr/bin/env python3
"""Reject Han characters in strings rendered by the en-US LVGL product UI."""

from pathlib import Path
import re
import sys


REPO = Path(__file__).resolve().parents[1]
SOURCE_ROOT = REPO / "apps" / "lv_port_linux" / "src"
HAN = re.compile(r"[\u3400-\u4dbf\u4e00-\u9fff]")

# The free-chat view accepts both current English messages and legacy Chinese
# speaker prefixes.  These tokens are input compatibility data and are stripped
# before rendering; they are not shipping UI copy.
ALLOWED_COMPATIBILITY_LITERALS = {
    "apps/lv_port_linux/src/apps/ai/ai_free_chat_view.c": {
        "我：",
        "我:",
        "助手：",
        "助手:",
    },
}


def c_string_literals(source: str):
    """Yield (line, decoded-ish contents) while skipping C/C++ comments."""
    i = 0
    line = 1
    length = len(source)
    while i < length:
        char = source[i]
        next_char = source[i + 1] if i + 1 < length else ""
        if char == "\n":
            line += 1
            i += 1
            continue
        if char == "/" and next_char == "/":
            i += 2
            while i < length and source[i] != "\n":
                i += 1
            continue
        if char == "/" and next_char == "*":
            i += 2
            while i < length:
                if source[i] == "\n":
                    line += 1
                if source[i] == "*" and i + 1 < length and source[i + 1] == "/":
                    i += 2
                    break
                i += 1
            continue
        if char == "'":
            i += 1
            while i < length:
                if source[i] == "\\":
                    i += 2
                    continue
                if source[i] == "\n":
                    line += 1
                if source[i] == "'":
                    i += 1
                    break
                i += 1
            continue
        if char != '"':
            i += 1
            continue

        literal_line = line
        i += 1
        value = []
        while i < length:
            if source[i] == "\\" and i + 1 < length:
                value.extend((source[i], source[i + 1]))
                i += 2
                continue
            if source[i] == '"':
                i += 1
                break
            if source[i] == "\n":
                line += 1
            value.append(source[i])
            i += 1
        yield literal_line, "".join(value)


def main() -> int:
    locale_header = SOURCE_ROOT / "ui" / "locale_en_us.h"
    if not locale_header.is_file() or 'UI_LOCALE_ID             "en-US"' not in locale_header.read_text(encoding="utf-8"):
        print("English UI audit failed: en-US locale header is missing or invalid", file=sys.stderr)
        return 1

    violations = []
    for path in sorted(SOURCE_ROOT.rglob("*")):
        if path.suffix not in {".c", ".h", ".cc", ".cpp"}:
            continue
        relative = path.relative_to(REPO).as_posix()
        allowed = ALLOWED_COMPATIBILITY_LITERALS.get(relative, set())
        source = path.read_text(encoding="utf-8")
        for line, literal in c_string_literals(source):
            if HAN.search(literal) and literal not in allowed:
                violations.append((relative, line, literal))

    if violations:
        print("English UI audit failed; Han text remains in shipping string literals:", file=sys.stderr)
        for relative, line, literal in violations:
            print(f"  {relative}:{line}: {literal}", file=sys.stderr)
        return 1

    print("English UI audit: PASS (en-US; legacy speaker-prefix parsers allowlisted)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
