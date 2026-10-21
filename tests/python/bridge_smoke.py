import json
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "python"))

import libytdlp_bridge
from libytdlp_bridge import extract_json, versions


def check_versions():
    info = json.loads(versions())
    assert set(("bridge", "yt_dlp", "python")) <= set(info), info
    assert info["bridge"] == libytdlp_bridge.__version__
    assert info["yt_dlp"], "empty yt-dlp version"
    print("versions:", info)


def check_dumps_round_trip():
    from libytdlp_bridge.serialize import dumps

    text = dumps({"id": "x", "title": "ü", "tags": ("a", "b"), "n": None})
    parsed = json.loads(text)
    assert parsed["id"] == "x"
    assert parsed["title"] == "ü"
    assert text.isascii(), "bridge output must stay ascii-safe"
    print("dumps round trip ok")


def check_rejects_empty_url():
    try:
        extract_json("", "{}")
    except ValueError:
        print("empty url rejected ok")
        return
    raise AssertionError("empty url was accepted")


def check_rejects_non_object_options():
    try:
        extract_json("ly-bogus://example", "[1, 2]")
    except ValueError:
        print("non-object options rejected ok")
        return
    raise AssertionError("non-object options were accepted")


def check_runtime_info():
    from libytdlp_bridge import runtime_info

    info = json.loads(runtime_info("FULL_BUNDLED", "abc123", 1, False))
    assert info["backend"] == "embedded"
    assert info["deployment_profile"] == "FULL_BUNDLED"
    assert info["ytdlp_commit"] == "abc123"
    assert info["abi"] == 1
    assert info["host_owned"] is False
    assert info["yt_dlp"], "empty yt-dlp version"
    print("runtime info ok")


def check_cachedir_defaults_off():
    from libytdlp_bridge.download import _params as download_params
    from libytdlp_bridge.extract import _params as extract_params

    assert extract_params({})["cachedir"] is False
    assert download_params({}, [], None)["cachedir"] is False
    assert extract_params({"cachedir": "/tmp/x"})["cachedir"] == "/tmp/x"

    hook = download_params({}, [], None)["progress_hooks"]
    custom = download_params({"progress_hooks": ["user"]}, [], None)["progress_hooks"]
    assert len(hook) == 1 and callable(hook[0])
    assert custom[0] == "user" and len(custom) == 2 and callable(custom[1])
    print("cachedir defaults ok")


def check_unsupported_url():
    try:
        extract_json("ly-bogus://no-such-scheme-xyz", "{}")
    except Exception as caught:
        assert str(caught), "exception without a message"
        print("unsupported url rejected ok:", type(caught).__name__)
        return
    raise AssertionError("unsupported url was accepted")


def check_extractor_inventory():
    from libytdlp_bridge import extractor_names

    names = json.loads(extractor_names())
    assert isinstance(names, list), type(names)
    assert names == sorted(names), "extractor list must be sorted"
    assert len(names) > 1000, f"expected a full registry, got {len(names)}"
    assert "generic" in names, "generic extractor missing"
    assert "youtube" in names, "youtube extractor missing"
    print(f"extractor inventory ok: {len(names)} extractors")


def check_extractor_args_passthrough():
    from libytdlp_bridge.extract import _params
    from yt_dlp import YoutubeDL

    options = {"extractor_args": {"youtube": {"player_client": ["ios"]}}}

    params = _params(options)

    assert params["extractor_args"] == {"youtube": {"player_client": ["ios"]}}

    with YoutubeDL(params) as ydl:
        assert ydl.params["extractor_args"] == {"youtube": {"player_client": ["ios"]}}

    print("extractor args passthrough ok")


def check_merge_options():
    from libytdlp_bridge.download import _merge_options

    assert _merge_options("{}", "{}") == {}
    assert _merge_options("", "") == {}
    assert _merge_options("", "{}") == {}
    assert _merge_options("{}", "") == {}

    merged = _merge_options('{"a": 1, "b": 2}', '{"b": 3, "c": 4}')
    assert merged == {"a": 1, "b": 3, "c": 4}, merged

    assert _merge_options('{"a": 1}', "") == {"a": 1}

    for context_options, job_options in [
        ("[1, 2]", "{}"),
        ("{}", "[1, 2]"),
        ("42", "{}"),
        ("{}", "null"),
        ('"x"', "{}"),
    ]:
        try:
            _merge_options(context_options, job_options)
        except ValueError:
            pass
        else:
            raise AssertionError(
                f"non-object options were accepted: {context_options!r} {job_options!r}"
            )

    print("merge options ok")


if __name__ == "__main__":
    check_versions()
    check_dumps_round_trip()
    check_rejects_empty_url()
    check_rejects_non_object_options()
    check_cachedir_defaults_off()
    check_extractor_inventory()
    check_extractor_args_passthrough()
    check_merge_options()
    check_runtime_info()
    check_unsupported_url()
    print("bridge ok")
