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

Options go in as a JSON object string at context creation. Anything `YoutubeDL` accepts passes through, so check upstream docs for the full list. Downloads stay off for now, that switch is forced until the job API lands.

Python-side failures surface as `LY_E_EXTRACT`. Bad JSON or empty URLs are `LY_E_INVALID_ARGUMENT`. `LY_E_NETWORK` exists but nothing reports it yet, transport errors still arrive as extract errors.

## Checks

`./dev test` runs everything offline. UBSan is clean. AddressSanitizer trips over a system crypto library yt-dlp pulls in, that's their `dlopen` flags fighting the sanitizer, not our code. So CI runs UBSan only.

## Layout

```
include/libytdlp/   the C ABI. This is the contract.
native/             C++23 guts: interpreter lifetime, bridge calls, ABI surface.
python/             the adapter. Thin on purpose, no per-site code lives here.
tests/              C smoke test plus bridge checks. Offline by default.
bindings/rust/      not yet. M2.
```

## Pins

- yt-dlp `2026.08.19`, submodule at `third_party/yt-dlp`, unmodified.
- Test venv pins the same version from PyPI. Keep the two in sync.
- Needs CPython 3.11 or newer.

## License

Our code: Apache-2.0. yt-dlp stays Unlicense. See `THIRD_PARTY_NOTICES.md` before you ship anything.
