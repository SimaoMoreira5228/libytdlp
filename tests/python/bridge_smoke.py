import json
import os
import subprocess
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
    from libytdlp_bridge.options import merge_options

    assert merge_options("{}", "{}") == {}
    assert merge_options("", "") == {}
    assert merge_options("", "{}") == {}
    assert merge_options("{}", "") == {}

    merged = merge_options('{"a": 1, "b": 2}', '{"b": 3, "c": 4}')
    assert merged == {"a": 1, "b": 3, "c": 4}, merged

    assert merge_options('{"a": 1}', "") == {"a": 1}

    for context_options, job_options in [
        ("[1, 2]", "{}"),
        ("{}", "[1, 2]"),
        ("42", "{}"),
        ("{}", "null"),
        ('"x"', "{}"),
    ]:
        try:
            merge_options(context_options, job_options)
        except ValueError:
            pass
        else:
            raise AssertionError(
                f"non-object options were accepted: {context_options!r} {job_options!r}"
            )

    print("merge options ok")


def check_download_sandbox():
    import tempfile

    from libytdlp_bridge.download import _params as download_params
    from yt_dlp import YoutubeDL

    root = tempfile.mkdtemp()
    outside = tempfile.mkdtemp()

    try:
        params = download_params({"outtmpl": os.path.join(outside, "%(id)s.%(ext)s")}, [], None)

        assert params["outtmpl"].startswith(outside)
        assert "libytdlp" not in params

        with YoutubeDL(params):
            pass

        try:
            download_params(
                {
                    "libytdlp": {"restrict_to": "relative/dir"},
                    "outtmpl": os.path.join(root, "%(id)s.%(ext)s"),
                },
                [],
                None,
            )
        except ValueError:
            pass
        else:
            raise AssertionError("relative restrict_to was accepted")

        try:
            download_params(
                {
                    "libytdlp": {"restrict_to": root},
                    "outtmpl": os.path.join(root, "..", "escape-%(id)s.%(ext)s"),
                },
                [],
                None,
            )
        except ValueError:
            pass
        else:
            raise AssertionError("escaping outtmpl was accepted")

        try:
            download_params(
                {
                    "libytdlp": {"restrict_to": root},
                    "outtmpl": os.path.join(outside, "out-%(id)s.%(ext)s"),
                },
                [],
                None,
            )
        except ValueError:
            pass
        else:
            raise AssertionError("outside outtmpl was accepted")

        try:
            download_params(
                {
                    "libytdlp": {"restrict_to": root},
                    "outtmpl": os.path.join(root, "%(id)s.%(ext)s"),
                    "paths": {"home": outside},
                },
                [],
                None,
            )
        except ValueError:
            pass
        else:
            raise AssertionError("outside paths home was accepted")

        try:
            download_params({"libytdlp": {"restrict_to": root}}, [], None)
        except ValueError:
            pass
        else:
            raise AssertionError("restriction without outtmpl or paths was accepted")

        params = download_params(
            {
                "libytdlp": {"restrict_to": root},
                "outtmpl": os.path.join(root, "%(id)s.%(ext)s"),
            },
            [],
            None,
        )

        assert params["outtmpl"].startswith(root)
        assert "libytdlp" not in params

        with YoutubeDL(params):
            pass

        print("download sandbox ok")
    finally:
        os.rmdir(root)
        os.rmdir(outside)


def fixture_script():
    here = os.path.dirname(os.path.abspath(__file__))

    return os.path.normpath(os.path.join(here, "..", "fixtures", "slow_server.py"))


def start_fixture_server(payload_bytes, rate_bytes_per_sec):
    process = subprocess.Popen(
        [sys.executable, fixture_script(), "0", str(payload_bytes), str(rate_bytes_per_sec)],
        stdout=subprocess.PIPE,
        text=True,
    )

    line = process.stdout.readline()

    if not line.startswith("PORT "):
        stop_fixture_server(process)

        raise AssertionError(f"fixture server did not report a port: {line!r}")

    port = int(line.removeprefix("PORT ").strip())

    return process, port


def stop_fixture_server(process):
    if process.poll() is None:
        process.terminate()

    try:
        process.wait(timeout=10)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=10)

    if process.stdout is not None:
        process.stdout.close()


def run_cli_info(url):
    return subprocess.run(
        [sys.executable, "-m", "yt_dlp", "--no-cache-dir", "-J", url],
        capture_output=True,
        text=True,
        timeout=120,
    )


def format_urls(info):
    urls = set()

    direct = info.get("url")

    if direct:
        urls.add(direct)

    for entry in info.get("formats") or []:
        candidate = entry.get("url")

        if candidate:
            urls.add(candidate)

    return urls


def check_cli_library_parity():
    server, port = start_fixture_server(65536, 8000000)

    try:
        url = f"http://127.0.0.1:{port}/file.bin"

        completed = run_cli_info(url)

        assert completed.returncode == 0, completed.stderr[-2000:]

        cli_info = json.loads(completed.stdout)

        lib_info = json.loads(extract_json(url, "{}"))

        assert cli_info["id"] == lib_info["id"], (cli_info.get("id"), lib_info.get("id"))

        assert cli_info["id"] == "file", cli_info.get("id")

        assert cli_info["ext"] == lib_info["ext"], (cli_info.get("ext"), lib_info.get("ext"))

        assert format_urls(cli_info) == format_urls(lib_info)

        assert format_urls(lib_info) == {url}, format_urls(lib_info)

        bogus_cli = run_cli_info("ly-bogus://no-such-scheme-xyz")

        assert bogus_cli.returncode != 0

        try:
            extract_json("ly-bogus://no-such-scheme-xyz", "{}")
        except Exception as caught:
            assert str(caught), "exception without a message"
        else:
            raise AssertionError("bogus url was accepted by the library")

        print("cli/library parity ok")
    finally:
        stop_fixture_server(server)


def check_find_executables():
    import shutil
    import tempfile

    from libytdlp_bridge.extract import find_executable, find_executables

    found = find_executable("python3", None)
    assert found, "python3 must resolve on PATH"
    assert os.path.isabs(found)
    assert os.access(found, os.X_OK)

    scratch = tempfile.mkdtemp()
    try:
        assert find_executable("python3", [scratch]) is None

        located = find_executables([scratch])
        assert located["ffmpeg"] is None
        assert located["ffprobe"] is None

        python_path = shutil.which("python3")
        assert python_path, "python3 must resolve on PATH"
        python_dir = os.path.dirname(python_path)
        assert find_executable("python3", [python_dir]) == python_path
    finally:
        os.rmdir(scratch)

    from libytdlp_bridge import runtime_info

    info = json.loads(runtime_info("FULL_BUNDLED", "abc123", 1, False))
    assert "ffmpeg" in info
    assert "ffprobe" in info
    assert info["ffmpeg"] is None or isinstance(info["ffmpeg"], str)
    assert info["ffprobe"] is None or isinstance(info["ffprobe"], str)
    print("find executables ok")


if __name__ == "__main__":
    check_versions()
    check_dumps_round_trip()
    check_rejects_empty_url()
    check_rejects_non_object_options()
    check_cachedir_defaults_off()
    check_extractor_inventory()
    check_extractor_args_passthrough()
    check_merge_options()
    check_download_sandbox()
    check_cli_library_parity()
    check_runtime_info()
    check_unsupported_url()
    check_find_executables()
    print("bridge ok")
