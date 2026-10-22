#!/usr/bin/env python3
import json
import sys

path, want_version = sys.argv[1], sys.argv[2]

with open(path, encoding="utf-8") as handle:
    bom = json.load(handle)

names = [c.get("name", "") for c in bom.get("components", [])]

if "libytdlp" not in names:
    sys.exit(f"sbom at {path} has no libytdlp component")

got = next(c["version"] for c in bom["components"] if c.get("name") == "libytdlp")

if got != want_version:
    sys.exit(f"sbom libytdlp version {got} != {want_version}")

if "yt-dlp" not in names:
    sys.exit(f"sbom at {path} has no yt-dlp component")

bad = [n for n in names if n.lower() == "ffmpeg"]

if bad:
    sys.exit("sbom must not list a bundled FFmpeg component")
