# Compatibility

What actually runs where. A cell is **verified** only if someone ran it. Everything else is **unknown** unless it fails for a known reason.

Legend: `verified` (proof named) · `known-broken` (reason named) · `unknown` (not run).

## Pins behind the table

- Bundled yt-dlp: `2026.10.21`, submodule `6ccd9773`.
- Test-env yt-dlp (pyproject, `uv sync`): `2026.10.21`, same as the submodule. Keep the two in sync when the pin moves.

## Linux

| CPython | FULL_BUNDLED | BUNDLED_YTDLP_SYSTEM_PYTHON | SYSTEM_YTDLP_SYSTEM_PYTHON |
|---|---|---|---|
| 3.14.8 (distro) | verified | verified | verified |
| 3.12.12 (uv-managed) | partial (see note 1) | unknown | unknown |
| 3.13.8 (uv-managed) | unknown | partial (see note 1) | unknown |

Proof for the 3.14.8 row: full `ctest` (smoke, jobs, bridge, install-tree) green on all three profiles, each from a clean configure. The SYSTEM cell was also probed directly: `runtime_info` reports `yt_dlp 2026.08.19` (the system package) and a `sys_path` with no bundled checkout in it, while the other two profiles report `2026.10.21` from the submodule. CI jobs `build-test` (profile axis) and `python-versions` (3.12–3.14, FULL) re-prove this row and the version range on every push.

Note 1: uv-managed interpreters are relocatable builds with no usable install prefix, so plain embedding boot fails on them (`init_fs_encoding`, no `encodings` module) unless `PYTHONHOME` points at the interpreter dir. With that set, smoke + jobs + bridge pass on 3.12.12/FULL and 3.13.8/BUNDLED. `install-tree` on those interpreters is known-broken: the staged scrubbed-environment run (`env -i`) strips `PYTHONHOME` and the staged tree can't boot. This is a property of relocatable interpreters, not of any profile. Distro and setup-python interpreters have real prefixes and don't need the workaround.

## Windows / macOS

| CPython | FULL_BUNDLED | BUNDLED_YTDLP_SYSTEM_PYTHON | SYSTEM_YTDLP_SYSTEM_PYTHON |
|---|---|---|---|
| 3.12 | unknown | unknown | unknown |
| other | unknown | unknown | unknown |

The existing `windows` and `macos` CI jobs build and test FULL only. Nobody ran the other two profiles there in this work, so they stay unknown. The profile fixes here are POSIX-neutral (string compares, install gating, test env), but that is not proof.

## Bounds worth knowing

- The staged scrubbed-environment run (`env -i`) keeps the test interpreter's directory on `PATH` and nothing else. Embedded `Py_Initialize` without a program name resolves `python3` through `PATH` for its path computation; a `PATH` with no interpreter in it degrades newer CPython releases (3.14 refuses to guess `lib-dynload`, older versions fall back). `LIBYTDLP_*` and `PYTHON*` stay stripped — that independence is what the run proves.

- `SYSTEM_YTDLP_SYSTEM_PYTHON` fails at configure time when `yt_dlp` isn't importable on the embed interpreter. Loud and early, by design. In CI the `build-test` SYSTEM leg pip-installs the pinned version first.
- `SYSTEM_YTDLP_SYSTEM_PYTHON` ignores the compiled-in and staged yt-dlp paths. An explicit `LIBYTDLP_YTDLP_DIR` still wins if you set one.
- Only `FULL_BUNDLED` installs the bridge plus the pinned yt-dlp sources. The other two profiles install the bridge plus (BUNDLED) the yt-dlp sources, and every profile runs on the system interpreter with site packages visible. No profile ships an interpreter.
- The `jobs` suite flaked once locally under the SYSTEM config (passed on immediate rerun and every run since). Fixture timing is the suspect. Not profile-specific as far as anyone can tell.
