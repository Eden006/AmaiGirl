#!/usr/bin/env python3
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

CPP_EXTS = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx"}
PREPROC_RE = re.compile(r"^\s*#\s*(if|ifdef|ifndef|elif)\b(?P<rest>.*)$")

PLATFORMS = {
    "windows": {
        "label": "Windows",
        "allowed_token": "Q_OS_WIN32",
        "token_pattern": re.compile(r"\bQ_OS_WIN[A-Z0-9_]*\b"),
        "defined_pattern": re.compile(r"defined\s*\(\s*Q_OS_WIN32\s*\)"),
    },
    "linux": {
        "label": "Linux",
        "allowed_token": "Q_OS_LINUX",
        "token_pattern": re.compile(r"\bQ_OS_LINUX\b"),
        "defined_pattern": re.compile(r"defined\s*\(\s*Q_OS_LINUX\s*\)"),
    },
    "macos": {
        "label": "macOS",
        "allowed_token": "Q_OS_MACOS",
        "token_pattern": re.compile(r"\bQ_OS_MAC[A-Z0-9_]*\b"),
        "defined_pattern": re.compile(r"defined\s*\(\s*Q_OS_MACOS\s*\)"),
    },
}


def iter_cpp_files(root: Path) -> list[Path]:
    files: list[Path] = []
    for path in root.rglob("*"):
        if path.is_file() and path.suffix.lower() in CPP_EXTS:
            files.append(path)
    return files


def check_file(path: Path, workspace: Path, platform: str) -> list[str]:
    violations: list[str] = []
    cfg = PLATFORMS[platform]
    label = cfg["label"]
    allowed_token = cfg["allowed_token"]
    token_pattern = cfg["token_pattern"]
    defined_pattern = cfg["defined_pattern"]

    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except UnicodeDecodeError:
        lines = path.read_text(encoding="utf-8", errors="replace").splitlines()

    rel = path.relative_to(workspace)

    for lineno, line in enumerate(lines, start=1):
        m = PREPROC_RE.match(line)
        if not m:
            continue

        directive = m.group(1)
        rest = m.group("rest")
        tokens = token_pattern.findall(line)
        if not tokens:
            continue

        unsupported = [token for token in tokens if token != allowed_token]
        if unsupported:
            uniq = ", ".join(sorted(set(unsupported)))
            violations.append(
                f"{rel}:{lineno}: 使用了不允许的 {label} 宏 {uniq}；仅允许 {allowed_token}，且写法应为 #if defined({allowed_token}) / #elif defined({allowed_token})。"
            )
            continue

        if directive in ("ifdef", "ifndef"):
            violations.append(
                f"{rel}:{lineno}: 禁止使用 #{directive} {allowed_token}；请改为 #if defined({allowed_token}) 或 #if !defined({allowed_token})。"
            )
            continue

        if directive in ("if", "elif") and not defined_pattern.search(rest):
            violations.append(
                f"{rel}:{lineno}: {label} 条件判断必须使用 defined({allowed_token}) 形式。"
            )

    return violations


def main() -> int:
    parser = argparse.ArgumentParser(description="检查平台宏写法是否符合项目规范。")
    parser.add_argument("--root", default="src", help="扫描目录，默认 src")
    parser.add_argument(
        "--platform",
        default="all",
        choices=["all", "windows", "linux", "macos"],
        help="检查的平台，默认 all",
    )
    args = parser.parse_args()

    workspace = Path.cwd()
    root = workspace / args.root
    if not root.exists():
        print(f"[check_platform_macro_style] 跳过：目录不存在: {root}")
        return 0

    files = iter_cpp_files(root)
    targets = [args.platform] if args.platform != "all" else list(PLATFORMS.keys())

    all_violations: list[str] = []
    for path in files:
        for platform in targets:
            all_violations.extend(check_file(path, workspace, platform))

    if all_violations:
        print("[check_platform_macro_style] 发现不符合规范的平台宏写法：")
        for msg in all_violations:
            print(f"  - {msg}")
        return 1

    print(
        f"[check_platform_macro_style] 通过：platform={args.platform}，已检查 {len(files)} 个文件，未发现违规写法。"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
