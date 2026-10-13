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
    # Exact input vocabulary and punctuation consumed by the command parser.
    # Keep the exception scoped to this file and these tokens: it must not
    # exempt new UI labels or arbitrary Chinese strings in the voice app.
    "apps/lv_port_linux/src/apps/voice/voice_command.c": {
        "。", "！", "返回首页", "回到首页", "打开应用抽屉", "打开应用列表",
        "打开设置", "打开无线网络", "打开wifi", "打开wi-fi", "打开蓝牙",
        "打开视频", "打开设备配对", "打开涂鸦智家",
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


def bilingual_helper_spans(source: str):
    """Recognize literal pairs only when tr has the verified locale selector."""
    string = r'"(?:\\.|[^"\\])*"'
    tokens = string + r"|'(?:\\.|[^'\\])*'|//[^\n]*|/\*[\s\S]*?\*/"
    code = re.sub(tokens, lambda m: re.sub(r"[^\n]", " ", m[0])
                  if m[0].startswith(("//", "/*")) else m[0], source)
    helper = (r"static\s+const\s+char\s*\*\s*tr\s*\(\s*const\s+char\s*\*\s*(\w+)"
              r"\s*,\s*const\s+char\s*\*\s*(\w+)\s*\)\s*\{\s*return\s+"
              r"ui_i18n_is_zh\s*\(\s*\)\s*\?\s*\1\s*:\s*\2\s*;\s*\}")
    if not re.search(helper, code):
        return []
    expression = string + r'(?:\s*' + string + r')*'
    call = re.compile(r'\btr\s*\(\s*(' + expression + r')\s*,\s*(' + expression + r')\s*\)')
    return [(m.start(1), m.end(1)) for m in call.finditer(code)
            if ASCII_COPY.search(m[2]) and not HAN.search(m[2])]


def c_string_literals(source: str, with_offsets=False):
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
        literal_offset = i
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
        item = (literal_line, "".join(value))
        yield (*item, literal_offset) if with_offsets else item


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
        helper_spans = bilingual_helper_spans(source)
        for line, literal, offset in c_string_literals(source, with_offsets=True):
            if not HAN.search(literal) or literal in allowed:
                continue
            if relative == BILINGUAL_CATALOG or \
                    is_explicit_bilingual_branch(lines, line) or \
                    any(start <= offset < end for start, end in helper_spans):
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
