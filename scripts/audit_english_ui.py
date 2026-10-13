#!/usr/bin/env python3
"""Reject unlocalized Han strings in the bilingual LVGL product UI."""

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

BILINGUAL_CATALOG = "apps/lv_port_linux/src/ui/ui_i18n.c"
ASCII_COPY = re.compile(r'"[^"\n]*[A-Za-z][^"\n]*"')


def is_explicit_bilingual_branch(lines, line_number: int) -> bool:
    """Accept nearby `ui_i18n_is_zh() ? zh : en` copy, including wraps."""
    start = max(0, line_number - 11)
    end = min(len(lines), line_number + 10)
    window = "\n".join(lines[start:end])
    return "ui_i18n_is_zh()" in window and ASCII_COPY.search(window) is not None


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
        lines = source.splitlines()
        for line, literal in c_string_literals(source):
            if not HAN.search(literal) or literal in allowed:
                continue
            if relative == BILINGUAL_CATALOG or \
                    is_explicit_bilingual_branch(lines, line):
                continue
            if HAN.search(literal):
                violations.append((relative, line, literal))

    if violations:
        print("Bilingual UI audit failed; unlocalized Han text remains in shipping string literals:", file=sys.stderr)
        for relative, line, literal in violations:
            print(f"  {relative}:{line}: {literal}", file=sys.stderr)
        return 1

    print("Bilingual UI audit: PASS (zh-CN/en-US catalog and explicit fallbacks)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
