#!/usr/bin/env python3
"""Update the bundled yt-dlp and matching test-environment pin."""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
VERSION_RE = re.compile(r"^\d{4}\.\d{2}\.\d{2}$")
COMMIT_RE = re.compile(r"^[0-9a-f]{40}$")


def replace_once(path, pattern, replacement):
    text = path.read_text(encoding="utf-8")
    updated, count = re.subn(pattern, replacement, text, count=1, flags=re.MULTILINE)
    if count != 1:
        raise SystemExit(f"expected one matching pin in {path.relative_to(ROOT)}")
    path.write_text(updated, encoding="utf-8")


def main(version, commit):
    if not VERSION_RE.fullmatch(version):
        raise SystemExit(f"invalid yt-dlp release version: {version}")
    if not COMMIT_RE.fullmatch(commit):
        raise SystemExit("expected a full 40-character yt-dlp commit hash")

    pyproject = ROOT / "pyproject.toml"
    text = pyproject.read_text(encoding="utf-8")
    match = re.search(r'yt-dlp==([0-9.]+)', text)
    if match is None:
        raise SystemExit("could not find the yt-dlp test dependency in pyproject.toml")
    previous = match.group(1)
    if previous == version:
        raise SystemExit(f"yt-dlp {version} is already pinned")

    replace_once(pyproject, r"yt-dlp==[0-9.]+", f"yt-dlp=={version}")

    for name in ("README.md", "COMPATIBILITY.md"):
        path = ROOT / name
        text = path.read_text(encoding="utf-8")
        path.write_text(text.replace(previous, version), encoding="utf-8")

    replace_once(
        ROOT / "COMPATIBILITY.md",
        r"(Bundled yt-dlp: `[^`]+`, submodule `)[0-9a-f]+(`\.)",
        rf"\g<1>{commit[:8]}\g<2>",
    )
    print(f"updated yt-dlp pin {previous} -> {version} ({commit[:8]})")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit("usage: tools/update-ytdlp.py VERSION COMMIT")
    main(sys.argv[1], sys.argv[2])
