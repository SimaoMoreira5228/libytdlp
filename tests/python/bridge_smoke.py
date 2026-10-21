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


def check_unsupported_url():
    try:
        extract_json("ly-bogus://no-such-scheme-xyz", "{}")
    except Exception as caught:
        assert str(caught), "exception without a message"
        print("unsupported url rejected ok:", type(caught).__name__)
        return
    raise AssertionError("unsupported url was accepted")


if __name__ == "__main__":
    check_versions()
    check_dumps_round_trip()
    check_rejects_empty_url()
    check_rejects_non_object_options()
    check_runtime_info()
    check_unsupported_url()
    print("bridge ok")
