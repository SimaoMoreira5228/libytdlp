# libytdlp

You want yt-dlp as a library, not a subprocess you shell out to. That's what this is.

One shared library, one C ABI, yt-dlp's real extractors running inside your process. No CLI parsing. No scraping stdout.

## The catch

Python doesn't go away. yt-dlp is Python, so the library embeds CPython and drives `YoutubeDL.extract_info` through a small adapter. You either point it at a yt-dlp checkout or let it use the one on your Python path. Short version: if `import yt_dlp` works, you're fine.

The interpreter boots on first use and stays up until the process exits. Finalizing and rebooting CPython mid-process breaks yt-dlp's dependencies. Don't fight it.

Calls sharing a context run one at a time. Threads are welcome. Concurrent extraction on the same handle just queues.

## Build it

```bash
git clone --recurse-submodules https://github.com/your-org/libytdlp.git
cd libytdlp
uv sync
./dev build
./dev test
```

`uv sync` gives you the pinned yt-dlp test env. CMake finds your system Python for the embed side. You need its dev headers (`python3-dev` on Debian-likes).

Run one extraction by hand:

```bash
BUILD_DIR=build ./dev run ly_smoke
```

Set `LIBYTDLP_TEST_URL` to something downloadable and the smoke test will hit the network too. Without it, everything runs offline.

## Use it

```c
ly_context* ctx = NULL;
ly_error* err = NULL;

if (ly_context_create(NULL, 0, &ctx, &err) != LY_OK) {
    fprintf(stderr, "%s\n", ly_error_message(err));
    ly_error_release(err);
    return 1;
}

ly_result* res = NULL;
if (ly_extract(ctx, url, strlen(url), &res, &err) != LY_OK) {
    fprintf(stderr, "%s\n", ly_error_message(err));
    ly_error_release(err);
    ly_context_release(ctx);
    return 1;
}

const char* json = NULL;
size_t len = 0;
ly_result_json(res, &json, &len);
fwrite(json, 1, len, stdout);

ly_result_release(res);
ly_context_release(ctx);
```

Output handles come back NULL on failure. The JSON pointer is borrowed, it dies with the result. Releasing NULL is safe. Error messages are borrowed the same way.

Options go in as a JSON object string at context creation. Anything `YoutubeDL` accepts passes through, so check upstream docs for the full list.

Python-side failures surface as `LY_E_EXTRACT`. Bad JSON or empty URLs are `LY_E_INVALID_ARGUMENT`. `LY_E_NETWORK` exists but nothing reports it yet, transport errors still arrive as extract errors.

## Jobs

Downloads run as jobs. You start one, poll it for progress, then wait for the final result. Or skip polling and just wait.

```c
ly_job* job = NULL;
if (ly_download_start(ctx, url, strlen(url), &job, &err) != LY_OK) {
    fprintf(stderr, "%s\n", ly_error_message(err));
    ly_error_release(err);
    return 1;
}

for (;;) {
    ly_job_state state = LY_JOB_RUNNING;
    ly_result* events = NULL;
    ly_job_poll(job, &state, &events, NULL);

    const char* data = NULL;
    size_t len = 0;
    ly_result_json(events, &data, &len);
    printf("state %d, %.*s\n", state, (int)len, data);
    ly_result_release(events);

    if (state != LY_JOB_RUNNING) {
        break;
    }
}

ly_result* res = NULL;
if (ly_job_wait(job, &res, &err) != LY_OK) {
    fprintf(stderr, "%s\n", ly_error_message(err));
    ly_error_release(err);
    ly_job_release(job);
    return 1;
}

ly_job_release(job);
```

`ly_job_poll` never blocks. It hands you the current state plus every progress event since your last poll, as a JSON array. Once the job is done the array is always empty, so drain what you need while it's running. `ly_job_wait` blocks until the end and gives you a fresh info result each time you call it, same deal on repeat calls. Call it as often as you like.

Cancel with `ly_job_cancel`. It's a suggestion, not a kill switch: yt-dlp only notices between chunks, so a fast download can finish before your cancel lands. In that case you get `DONE`, not `CANCELLED`, which is the honest answer. Hammer it all you want, it's idempotent, and calling it on a finished job is a no-op.

`ly_job_release` on a running job waits for the worker to finish, so don't call it from a thread that's in a hurry. Releasing NULL is fine, like the other release functions. Everything else rejects a NULL job.

Where files land is your call. Pass `outtmpl` (and `paths`, `format`, whatever else) through the context options and yt-dlp does the rest. The library doesn't fence you into a directory in M3, so pick templates that stay where you expect. Same goes for FFmpeg: still external, still found on `PATH` unless you point elsewhere. Downloads that need postprocessing fail without it.

## Experimental process backend

The Rust bindings also ship a prototype that downloads through a real `yt-dlp` child process instead of the embedded interpreter: `ProcessClient` and `ProcessDownloadJob` in `libytdlp::process`. Same shape as the native jobs (`download`, `poll`, `wait`, `cancel`), different stop: cancel kills the whole process tree, so a stalled transfer dies in milliseconds.

It finds `yt-dlp` on `PATH`, or you hand it an exact binary. Only `outtmpl` (literal paths, no template fields) and `proxy` options exist. Failures map to the usual `Kind` values. Timeouts cover the whole wait.

This is research, not a second API. It exists to test whether a killable subprocess backend belongs behind the C ABI later. Expect it to move or disappear.

## Checks

`./dev test` runs everything offline. UBSan is clean. AddressSanitizer trips over a system crypto library yt-dlp pulls in, that's their `dlopen` flags fighting the sanitizer, not our code. So CI runs UBSan only.

## Profiles

Three ways to run this, same API in all of them.

`FULL_BUNDLED` is the default and the one you want for shipping apps. Pinned yt-dlp travels with the library, no user installs. `BUNDLED_YTDLP_SYSTEM_PYTHON` uses the distro Python instead. `SYSTEM_YTDLP_SYSTEM_PYTHON` leans on whatever the machine already has. Good for development. You own the version match there.

Set it at configure time: `cmake -DLIBYTDLP_DEPLOYMENT_PROFILE=BUNDLED_YTDLP_SYSTEM_PYTHON`. Ask the library what it ended up with through `ly_version_info`, or `runtime_info()` in Rust. Same JSON either way.

What lands in the prefix differs. `FULL_BUNDLED` installs the bridge and the pinned yt-dlp sources. `BUNDLED_YTDLP_SYSTEM_PYTHON` installs the same two and runs on the distro Python. `SYSTEM_YTDLP_SYSTEM_PYTHON` installs just the bridge and imports `yt_dlp` from your interpreter. Configure fails outright if it can't import it there, so you find out before building, not at first extraction.

Every profile runs on a system-provisioned interpreter, site packages and all. `SYSTEM_YTDLP_SYSTEM_PYTHON` also ignores the bundled yt-dlp checkout entirely, unless you force one in with `LIBYTDLP_YTDLP_DIR`. libytdlp never ships its own interpreter; if your deployment can't use the system one, provision it with your distro, `setup-python`, or python-build-standalone.

CI builds and tests all three profiles on Linux, plus CPython 3.12 through 3.14 on the default profile. Windows and macOS jobs still cover `FULL_BUNDLED` only. The full matrix of what was actually run lives in `COMPATIBILITY.md`.

FFmpeg is never bundled. yt-dlp finds `ffmpeg` on `PATH` like always, or you hand a location to one context through the normal options. A missing FFmpeg only breaks the operations that need it. Nothing else.

## Install it once

Build-tree paths and `LIBYTDLP_LIB_DIR` are for development. To use the library from another project, install it:

```bash
cmake -S . -B build
cmake --build build
cmake --install build --prefix /usr/local
```

That prefix then holds everything a consumer needs: headers, the shared and static libraries, a versioned CMake package, the Python bridge, the pinned yt-dlp sources, the SBOM, and the license notices. The installed library finds its bridge and yt-dlp next to itself, so `LIBYTDLP_BRIDGE_DIR` and `LIBYTDLP_YTDLP_DIR` stay unset. Move the whole prefix somewhere else and it still works, it resolves everything relative to the loaded library.

Under the prefix:

```
include/libytdlp/          the C ABI headers
lib|lib64/libytdlp.so*     shared library
lib|lib64/libytdlp.a       static archive
lib|lib64/cmake/libytdlp/  find_package config, versioned
bin/ytdlp.dll              shared library (Windows)
share/libytdlp/python/     the bridge, used automatically
share/libytdlp/yt-dlp/     pinned yt-dlp sources, used automatically
share/libytdlp/sbom.json   generated at install time from pinned sources
share/doc/libytdlp/        LICENSE, NOTICE, THIRD_PARTY_NOTICES.md
```

No FFmpeg in there, ever. Every profile uses an external executable.

### From C or C++

```cmake
find_package(libytdlp REQUIRED)
target_link_libraries(myapp PRIVATE libytdlp::ytdlp)
```

Pass `-DCMAKE_PREFIX_PATH=/your/prefix` at configure time when the install isn't on a system path. `libytdlp::ytdlp` is the shared library, `libytdlp::ytdlp_static` is the static one. Both are installed, and the static target carries its Threads, Python, and C++ runtime linkage with it, so plain C projects link it without extra flags. The shared library finds the staged tree next to itself. A static link has no library path to anchor to, so point it at the tree explicitly: `LIBYTDLP_BRIDGE_DIR=<prefix>/share/libytdlp/python` and `LIBYTDLP_YTDLP_DIR=<prefix>/share/libytdlp/yt-dlp`.

### From Rust

Install the library somewhere system-wide (`/usr/local` works, verified end to end) and depend on `libytdlp` like any other crate. No `LIBYTDLP_LIB_DIR`, no `build.rs` on your side. `libytdlp-sys` looks in `/usr/lib`, `/usr/lib64`, `/usr/local/lib`, and `/usr/local/lib64` by itself, and only asks for `LIBYTDLP_LIB_DIR` when nothing is installed. Static linking wins when `libytdlp.a` is there. `LIBYTDLP_LINK=shared` switches to the `.so`, and then the loader has to find it like any shared library.

### After changing native code

Rebuild, reinstall to the same prefix, rebuild the consumer. No reconfigure unless CMake options changed:

```bash
cmake --build build
cmake --install build --prefix /usr/local
```

Static consumers absorb the archive, so `cargo build` relinks on the next run (the `sys` build script watches the installed archive). Shared consumers pick the new library up at process start.

## Layout

```
include/libytdlp/   the C ABI. This is the contract.
native/             C++23 guts: interpreter lifetime, bridge calls, ABI surface.
python/             the adapter. Thin on purpose, no per-site code lives here.
tests/              C smoke test plus bridge checks. Offline by default.
bindings/rust/      `libytdlp-sys` (raw FFI) and `libytdlp` (safe wrapper).
```

## Rust bindings

Two crates live in `bindings/rust`. `libytdlp-sys` is the raw FFI. `libytdlp` is the wrapper you actually want.

Cargo builds the native library itself on first use. No manual CMake step, no exports:

```bash
cd bindings/rust
cargo test
```

The build script picks a fresh `<repo>/build` when one exists, otherwise it configures and builds the static library from the bundled sources (needs `cmake`, a C++23 compiler, and Python development headers). `LIBYTDLP_LIB_DIR` overrides that when your build directory lives elsewhere, and fails loudly if it holds no built library. If the directory holds `libytdlp.a`, your binary absorbs it statically and you ship one file. Only `libpython` and `libstdc++` stay dynamic, and the build script finds both itself. No `build.rs` needed on your side.

`LIBYTDLP_LINK=shared` forces the `.so` instead, for when you want one shared copy across processes. Then the loader has to find it: install system-wide or bake an rpath yourself. `LIBYTDLP_LINK=static` fails loudly if no archive is around. `LIBYTDLP_PYTHON` picks the interpreter used for Python linkage, default `python3`. Leave `LIBYTDLP_LIB_DIR` unset and the build script checks your repo `build/` directory first, then a system-wide install.

```rust
let client = libytdlp::Client::new("{}")?;
let info = client.extract("https://example.org/media")?;
println!("{}", info.title().unwrap_or("unknown"));
```

Options are a JSON string passed straight to `YoutubeDL`, same as the C API. Offline tests run by default. Set `LIBYTDLP_TEST_URL` and the suite hits the network too, just like the C smoke test. `LIBYTDLP_TEST_SEARCH` enables one live `ytsearch` query on top. `runtime_info()` reports backend versions, the deployment profile, and whether the interpreter is host-owned.

`extract` blocks the calling thread. From tokio, wrap it in `spawn_blocking` against one shared `Client` and await that. There is no async wrapper yet, and the native side serializes concurrent calls anyway, so a dedicated blocking pool is the honest adapter.

CMake can build the crates as part of the unified build with `-DLIBYTDLP_BUILD_RUST=ON` (pinned Corrosion). Off by default. Plain `cargo build` never needs CMake or Corrosion.

## Resolve example

`examples/resolve` is the whole flow in one binary: extract a URL, pick the best audio-only stream, print it.

```bash
cmake -S . -B build
cmake --build build
cd examples/resolve
cargo run -- https://example.org/media
```

It prints `title`, `duration_secs`, `uploader`, and `url`, one `key=value` per line. Audio-only means a format with an audio codec and no video codec, highest bitrate wins. If nothing qualifies, it falls back to the first format with a URL.

No extra crates, just `std` and `libytdlp`. To try it offline, serve a file with `tests/fixtures/slow_server.py` and point it at that. `cargo test` in that directory does exactly that against the fixture server.

## Pins

- yt-dlp `2026.10.21`, submodule at `third_party/yt-dlp`, unmodified.
- Test venv pins the same version from PyPI. Keep the two in sync.
- Needs CPython 3.11 or newer.

## License

Our code: Apache-2.0. yt-dlp stays Unlicense. See `THIRD_PARTY_NOTICES.md` before you ship anything.
