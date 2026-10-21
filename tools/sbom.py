#!/usr/bin/env python3
import argparse
import datetime
import json
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
RUST_WORKSPACES = (
    ("bindings/rust", "bindings/rust/Cargo.toml"),
    ("examples/resolve", "examples/resolve/Cargo.toml"),
)
YTDLP_REPO = "https://github.com/yt-dlp/yt-dlp"
CRATES_URL = "https://crates.io/crates"
PYTHON_ORIGIN = "https://www.python.org"

ALLOW_IDS = frozenset({
    "Apache-2.0",
    "MIT",
    "MIT-0",
    "BSD-2-Clause",
    "BSD-3-Clause",
    "ISC",
    "PSF-2.0",
    "Unlicense",
})
ALLOW_NAMES = frozenset({
    "Python Software Foundation License",
    "Unicode-3.0",
})
KEYWORDS = frozenset({"OR", "AND", "WITH"})
ATOM = re.compile(r"[A-Za-z0-9][A-Za-z0-9.+\-]*")
LICENSE_LINE = re.compile(r"^license\s*=\s*[\"']([^\"']+)[\"']")


def run_capture(argv, cwd=ROOT):
    try:
        proc = subprocess.run(argv, capture_output=True, text=True, cwd=cwd)
    except OSError:
        return None
    if proc.returncode != 0:
        return None
    return proc.stdout


def native_version():
    return (ROOT / "VERSION").read_text(encoding="utf-8").strip()


def ytdlp_version():
    text = (ROOT / "third_party/yt-dlp/yt_dlp/version.py").read_text(encoding="utf-8")
    for raw in text.splitlines():
        line = raw.strip()
        match = re.match(r"^_pkg_version\s*=\s*'([^']+)'", line)
        if match:
            return match.group(1)
    match = re.search(r"^__version__\s*=\s*'([^']+)'", text, re.MULTILINE)
    return match.group(1) if match else "UNKNOWN"


def ytdlp_commit():
    out = run_capture(["git", "-C", "third_party/yt-dlp", "rev-parse", "HEAD"])
    return out.strip() if out else "UNKNOWN"


def python_version():
    info = sys.version_info
    return f"{info.major}.{info.minor}.{info.micro}"


def split_atoms(expr):
    return [t for t in ATOM.findall(expr or "") if t.upper() not in KEYWORDS]


def parse_lock(path):
    entries = []
    current = None
    with open(path, encoding="utf-8") as handle:
        for raw in handle:
            line = raw.strip()
            if line == "[[package]]":
                if current:
                    entries.append(current)
                current = {}
            elif current is not None and "=" in line:
                key, _, value = line.partition("=")
                key = key.strip()
                if key in ("name", "version", "source", "checksum"):
                    current[key] = value.strip().strip('"')
    if current:
        entries.append(current)
    return entries


def registry_license(name, version):
    base = Path(os.environ.get("CARGO_HOME", str(Path.home() / ".cargo")))
    for parent in sorted((base / "registry" / "src").glob("*")):
        manifest = parent / f"{name}-{version}" / "Cargo.toml"
        if not manifest.is_file():
            continue
        for raw in manifest.read_text(encoding="utf-8", errors="replace").splitlines():
            match = LICENSE_LINE.match(raw.strip())
            if match:
                return match.group(1)
    return ""


def cargo_packages(manifest):
    out = run_capture([
        "cargo", "metadata", "--format-version", "1", "--offline",
        "--manifest-path", str(ROOT / manifest),
    ])
    if out is None:
        return None
    try:
        doc = json.loads(out)
    except ValueError:
        return None
    if not isinstance(doc, dict):
        return None
    packages = doc.get("packages")
    return packages if isinstance(packages, list) else None


def blank_record(name, version):
    return {
        "name": name,
        "version": version,
        "license": "",
        "origin": "",
        "hash": "",
        "extras": {},
        "consumers": [],
    }


def merge_record(merged, record, consumer):
    key = (record["name"], record["version"])
    slot = merged.get(key)
    if slot is None:
        record["consumers"] = [consumer]
        merged[key] = record
        return
    if consumer not in slot["consumers"]:
        slot["consumers"].append(consumer)
    if not slot["license"] and record["license"]:
        slot["license"] = record["license"]
    if not slot["origin"] and record["origin"]:
        slot["origin"] = record["origin"]
    if not slot["hash"] and record["hash"]:
        slot["hash"] = record["hash"]
    for prop, value in record["extras"].items():
        slot["extras"].setdefault(prop, value)


def collect_rust(merged, provenance):
    for label, manifest in RUST_WORKSPACES:
        lock = parse_lock(ROOT / Path(manifest).parent / "Cargo.lock")
        checksums = {
            (e.get("name", ""), e.get("version", "")): e.get("checksum", "")
            for e in lock
        }
        packages = cargo_packages(manifest)
        if packages is None:
            provenance.append(f"{label}: lockfile+registry")
            for entry in lock:
                name = entry.get("name", "")
                version = entry.get("version", "")
                if not name or not version:
                    continue
                record = blank_record(name, version)
                record["hash"] = checksums.get((name, version), "")
                if entry.get("source", "").startswith("registry+"):
                    record["license"] = registry_license(name, version) or "NOASSERTION"
                    record["origin"] = f"{CRATES_URL}/{name}/{version}"
                else:
                    record["license"] = "Apache-2.0"
                merge_record(merged, record, label)
            continue
        provenance.append(f"{label}: cargo-metadata")
        for pkg in packages:
            name = pkg.get("name", "")
            version = pkg.get("version", "")
            if not name or not version:
                continue
            record = blank_record(name, version)
            record["license"] = pkg.get("license") or "NOASSERTION"
            if pkg.get("source") is None:
                record["origin"] = pkg.get("repository") or ""
            else:
                record["origin"] = f"{CRATES_URL}/{name}/{version}"
                record["hash"] = checksums.get((name, version), "")
            merge_record(merged, record, label)


def build_inventory():
    merged = {}
    provenance = []
    version = native_version()
    native = blank_record("libytdlp", version)
    native["license"] = "Apache-2.0"
    merge_record(merged, native, "native")

    collect_rust(merged, provenance)
    repo = ""
    for record in merged.values():
        if record["origin"] and not record["hash"]:
            repo = record["origin"]
            break
    for record in merged.values():
        if not record["origin"] and not record["hash"]:
            record["origin"] = repo

    ytdlp = blank_record("yt-dlp", ytdlp_version())
    ytdlp["license"] = "Unlicense"
    ytdlp["origin"] = YTDLP_REPO
    ytdlp["extras"] = {"commit": ytdlp_commit()}
    merge_record(merged, ytdlp, "native")

    cpython = blank_record("cpython", python_version())
    cpython["license"] = ""
    cpython["origin"] = PYTHON_ORIGIN
    cpython["extras"] = {"license-name": "Python Software Foundation License"}
    merge_record(merged, cpython, "native")

    records = [merged[key] for key in sorted(merged, key=lambda k: (k[0].lower(), k[1]))]
    offenders = [r for r in records if r["name"].lower() == "ffmpeg"]
    if offenders:
        print("sbom: refusing to emit bundled FFmpeg component", file=sys.stderr)
        sys.exit(2)
    return records, provenance


def encode_licenses(record):
    if record["extras"].get("license-name"):
        return [{"license": {"name": record["extras"]["license-name"]}}]
    atoms = split_atoms(record.get("license", ""))
    if not atoms:
        return [{"license": {"name": "NOASSERTION"}}]
    entries = []
    for atom in atoms:
        if atom in ALLOW_IDS:
            entries.append({"license": {"id": atom}})
        else:
            entries.append({"license": {"name": atom}})
    return entries


def license_expression(record):
    if record["extras"].get("license-name"):
        return record["extras"]["license-name"]
    return record.get("license", "")


def gate_problems(records):
    problems = []
    for record in records:
        expr = license_expression(record)
        if record["extras"].get("license-name"):
            atoms = [expr]
            bad = [a for a in atoms if a not in ALLOW_IDS and a not in ALLOW_NAMES]
        else:
            atoms = split_atoms(expr)
            bad = [a for a in atoms if a not in ALLOW_IDS and a not in ALLOW_NAMES]
            if not atoms:
                bad = ["(missing license)"]
        if bad:
            detail = ", ".join(bad)
            label = f"{record['name']}@{record['version']}"
            problems.append(f"{label}: {expr or '(missing)'} forbids {detail}")
    return problems


def to_component(record):
    component = {
        "bom-ref": f"{record['name']}@{record['version']}",
        "type": "library",
        "name": record["name"],
        "version": record["version"],
        "licenses": encode_licenses(record),
    }
    if record["hash"]:
        component["hashes"] = [{"alg": "SHA-256", "content": record["hash"]}]
    if record["origin"]:
        kind = "website"
        if record["name"] == "yt-dlp":
            kind = "vcs"
        elif record["hash"]:
            kind = "distribution"
        component["externalRefs"] = [{"type": kind, "url": record["origin"]}]
    properties = []
    if record["extras"].get("commit"):
        properties.append({"name": "commit", "value": record["extras"]["commit"]})
    if len(record["consumers"]) > 1:
        properties.append({"name": "consumers", "value": ",".join(sorted(record["consumers"]))})
    if properties:
        component["properties"] = properties
    return component


def build_bom(records, version):
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    return {
        "bomFormat": "CycloneDX",
        "specVersion": "1.6",
        "version": 1,
        "metadata": {
            "timestamp": stamp,
            "tools": {
                "components": [
                    {"type": "application", "name": "tools/sbom.py", "version": version}
                ]
            },
        },
        "components": [to_component(r) for r in records],
    }


def parse_args(argv):
    parser = argparse.ArgumentParser(prog="sbom")
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--emit", action="store_true")
    parser.add_argument("--out", metavar="FILE", default="")
    return parser.parse_args(argv)


def main(argv=None):
    args = parse_args(sys.argv[1:] if argv is None else argv)
    if args.check and args.emit:
        print("sbom: --check and --emit are mutually exclusive", file=sys.stderr)
        return 2
    version = native_version()
    records, provenance = build_inventory()
    for note in provenance:
        print(f"sbom: rust licenses via {note}", file=sys.stderr)
    if args.check:
        problems = gate_problems(records)
        if problems:
            for line in problems:
                print(f"sbom: {line}", file=sys.stderr)
            return 1
        print(f"sbom: license gate passed for {len(records)} components", file=sys.stderr)
        return 0
    bom = build_bom(records, version)
    text = json.dumps(bom, indent=2) + "\n"
    if args.out:
        Path(args.out).write_text(text, encoding="utf-8")
        return 0
    sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
