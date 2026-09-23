#!/usr/bin/env python3
"""Reject platform leakage into product-independent source layers."""

from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCAN_ROOTS = (ROOT / "core", ROOT / "frontends", ROOT / "services")
FORBIDDEN = (
    re.compile(r"/dev/(video|hidg|disp|cedar|ion)"),
    re.compile(r"\b(SOC_A133|SOC_A527|SOC_V883|SOC_RK3576|SOC_RV1106)\b"),
    re.compile(r"#\s*include\s*[<\"](?:sunxi|allwinner|rockchip|rk_|mpi/|mpp/)"),
)
CODE_SUFFIXES = {
    ".c", ".cc", ".cpp", ".h", ".hpp", ".go", ".rs", ".py", ".sh",
    ".js", ".jsx", ".ts", ".tsx", ".mk",
}
CODE_FILENAMES = {"CMakeLists.txt", "Makefile"}
HAN = re.compile(r"[\u3400-\u4dbf\u4e00-\u9fff]")
STRING_LITERAL = re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'')
PATH_TOKEN = re.compile(r"[^\s\"'`<>{}\[\](),;，。；：（）「」【】]+[/\\][^\s\"'`<>{}\[\](),;，。；：（）「」【】]+")


def candidate_files() -> list[Path]:
    result = subprocess.run(
        ["git", "ls-files", "--cached", "--others", "--exclude-standard", "-z"],
        cwd=ROOT,
        check=True,
        stdout=subprocess.PIPE,
    )
    files = []
    for raw in result.stdout.split(b"\0"):
        if not raw:
            continue
        path = ROOT / raw.decode("utf-8", "surrogateescape")
        relative = path.relative_to(ROOT).as_posix()
        if relative.startswith("third_party/") or not path.is_file():
            continue
        if path.suffix in CODE_SUFFIXES or path.name in CODE_FILENAMES:
            files.append(path)
    return files


def unicode_path_tokens(line: str) -> list[str]:
    violations = []
    for literal_match in STRING_LITERAL.finditer(line):
        literal = literal_match.group(0)[1:-1]
        for match in PATH_TOKEN.finditer(literal):
            token = match.group(0)
            if not HAN.search(token):
                continue
            path_like = (
                token.startswith(("/", "./", "../", "~", "$"))
                or re.search(r"\.[A-Za-z0-9]{1,10}(?:[/\\]|$)", token)
                or any(marker in line for marker in (
                    ".join(", "Path(", "PathBuf", "require_file", "require_dir",
                ))
            )
            if path_like:
                violations.append(token)
    return violations


def main() -> int:
    violations: list[str] = []
    for root in SCAN_ROOTS:
        if not root.exists():
            continue
        for path in root.rglob("*"):
            if not path.is_file() or path.suffix not in {".c", ".cc", ".cpp", ".h", ".py"}:
                continue
            text = path.read_text(encoding="utf-8", errors="replace")
            for line_number, line in enumerate(text.splitlines(), 1):
                if any(pattern.search(line) for pattern in FORBIDDEN):
                    violations.append(f"{path.relative_to(ROOT)}:{line_number}: {line.strip()}")
    for path in candidate_files():
        text = path.read_text(encoding="utf-8", errors="replace")
        for line_number, line in enumerate(text.splitlines(), 1):
            for token in unicode_path_tokens(line):
                violations.append(
                    f"{path.relative_to(ROOT)}:{line_number}: unicode path token: {token}"
                )
    if violations:
        print("architecture dependency check: FAIL", file=sys.stderr)
        print("\n".join(violations), file=sys.stderr)
        return 1
    print("architecture dependency check: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
