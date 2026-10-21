# Third-party notices

This file tracks the components libytdlp builds on. libytdlp's own code
is Apache-2.0 (see LICENSE). Nothing below is relicensed by us.

## yt-dlp (pinned submodule at third_party/yt-dlp)

- Origin: https://github.com/yt-dlp/yt-dlp
- Pinned tag: 2026.08.19 (commit 3a08beaf031ab68f966401ead017ac81fe8486cf)
- License: Unlicense (public-domain dedication with permissive fallback).
  Keep its upstream LICENSE file wherever the submodule ships.
- Note: upstream warns that some standalone PyInstaller executables they
  publish combine GPLv3+ code. We build from the Git source, not those
  executables, so that warning does not apply to this layout. Recheck it
  every time the pin moves or packaging changes.

## CPython

- License: Python Software Foundation License plus bundled notices.
- System-runtime mode links whatever CPython CMake finds (3.11+).
  Bundled-runtime packaging does not exist yet; when it does, ship the
  PSF license text with it.

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
- Transitive build deps (`serde_core`, `serde_derive`, `syn`,
  `quote`, `proc-macro2`, `unicode-ident`, `itoa`, `memchr`, `zmij`)
  are all permissive (MIT OR Apache-2.0, plus Unlicense for `memchr`
  and Unicode-3.0 for `unicode-ident`'s tables). Their build scripts
  only probe rustc cfgs and target widths. No C code, no network.
