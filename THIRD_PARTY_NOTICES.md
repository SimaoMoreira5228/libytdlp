# Third-party notices

This file tracks the components libytdlp builds on. libytdlp's own code
is Apache-2.0 (see LICENSE). Nothing below is relicensed by us.

## yt-dlp (pinned submodule at third_party/yt-dlp)

- Origin: https://github.com/yt-dlp/yt-dlp
- Pinned revision: 6ccd977303412151ee23352c6257dd658ffc1987 (release 2026.10.21; reported version 2026.10.21)
- License: Unlicense (public-domain dedication with permissive fallback).
  Keep its upstream LICENSE file wherever the submodule ships.
- Note: upstream warns that some standalone PyInstaller executables they
  publish combine GPLv3+ code. We build from the Git source, not those
  executables, so that warning does not apply to this layout. Recheck it
  every time the pin moves or packaging changes.

## CPython

- License: Python Software Foundation License plus bundled notices.
- System-runtime profiles link whatever CPython CMake finds (3.10+).
  Bundled-runtime packaging does not exist yet; when it does, ship the
  PSF license text with it.

## Runtime dependency policy

- libytdlp never vendors, compiles, or redistributes FFmpeg/ffprobe.
  Every profile uses an external executable found on `PATH` or passed
  per context. Keep FFmpeg out of artifact SBOMs; it may appear in a
  separate external-dependency manifest.
- `cmake --install` puts `LICENSE`, `NOTICE`, and this file under
  `<prefix>/share/doc/libytdlp`, and writes a generated `sbom.json`
  next to the bridge under `<prefix>/share/libytdlp`. The install-time
  SBOM comes from the pinned sources and refuses to list FFmpeg, same
  as the checked-in tooling.
- `mutagen` (GPL-2.0-or-later per yt-dlp's dependency listing) ships
  in no default bundle. Features that need it get a system/optional
  package plus a documented gap, not a silent downgrade.
- `certifi` (MPL-2.0) only matters when actually shipped; track its
  covered files then.
- Corrosion (MIT) is a build-time tool and is not shipped.

## Test/dev environment (uv)

- `pyproject.toml` pins `yt-dlp==2026.08.19` from PyPI for the local
  test venv. Same version as the submodule pin, different delivery.
  Keep them in sync.

## Rust dependencies (bindings/rust, exact versions in bindings/rust/Cargo.lock)

- `serde` (MIT OR Apache-2.0): derive-based parsing of extractor
  metadata. Pure Rust, no platform code. One `serde_json::Value`
  holds every extractor field, so no per-site structs are needed.
- `serde_json` (MIT OR Apache-2.0): parses the JSON the bridge
  returns into that `Value`. Pure Rust, no platform code. Unknown
  upstream fields are kept instead of dropped.
- `tracing` (MIT, `std` + `attributes` features only): spans around
  blocking FFI calls and `DEBUG`-only failure events carrying the error
  kind. Pure Rust, no platform code, no subscriber bundled. Callers
  control verbosity; values that may hold credentials (URLs, messages)
  are never recorded, only operation names, kinds, and counts.
- Transitive build deps (`serde_core`, `serde_derive`, `syn`,
  `quote`, `proc-macro2`, `unicode-ident`, `itoa`, `memchr`, `zmij`,
  `tracing-core`, `tracing-attributes`, `once_cell`,
  `pin-project-lite`) are all permissive: MIT and/or Apache-2.0,
  except Unlicense for `memchr` and Unicode-3.0 for
  `unicode-ident`'s tables. Their build scripts only probe rustc
  cfgs and target widths. No C code, no network.
