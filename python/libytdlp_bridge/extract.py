import json
import os
import shutil

import yt_dlp
from yt_dlp import YoutubeDL

from .options import merge_options
from .serialize import dumps

BRIDGE_VERSION = "0.2.0"


class _SilentLogger:
    def debug(self, message):
        pass

    def warning(self, message):
        pass

    def error(self, message):
        pass


def _params(options):
    params = {
        "quiet": True,
        "no_warnings": True,
        "logger": _SilentLogger(),
        "cachedir": False,
    }

    if options:
        if not isinstance(options, dict):
            raise ValueError("options must be a JSON object")
        params.update(options)

    params["skip_download"] = True
    return params


def extract_json(url, context_options_json="{}", call_options_json="{}"):
    if not url or not isinstance(url, str):
        raise ValueError("url must be a non-empty string")

    options = merge_options(context_options_json, call_options_json)

    with YoutubeDL(_params(options)) as ydl:
        info = ydl.extract_info(url, download=False)

    return dumps(info)


def find_executable(name, search_dirs=None):
    if not name or not isinstance(name, str):
        raise ValueError("name must be a non-empty string")

    if search_dirs is None:
        return shutil.which(name)

    for directory in search_dirs:
        candidate = os.path.join(directory, name)

        if os.path.isfile(candidate) and os.access(candidate, os.X_OK):
            return candidate

    return None


def find_executables(search_dirs=None):
    return {
        "ffmpeg": find_executable("ffmpeg", search_dirs),
        "ffprobe": find_executable("ffprobe", search_dirs),
    }


def versions():
    import platform

    return json.dumps(
        {
            "bridge": BRIDGE_VERSION,
            "yt_dlp": yt_dlp.version.__version__,
            "python": platform.python_version(),
        }
    )


def extractor_names():
    from yt_dlp.extractor import list_extractor_classes

    return json.dumps(sorted(ie.IE_NAME for ie in list_extractor_classes()))


def runtime_info(profile, ytdlp_commit, abi, host_owned):
    import sys

    versions_info = json.loads(versions())
    versions_info["wrapper"] = BRIDGE_VERSION
    versions_info["backend"] = "embedded"
    versions_info["deployment_profile"] = profile
    versions_info["ytdlp_commit"] = ytdlp_commit
    versions_info["abi"] = int(abi)
    versions_info["host_owned"] = bool(host_owned)
    versions_info["isolated"] = bool(sys.flags.isolated)
    versions_info["prefix"] = sys.prefix
    versions_info["sys_path"] = list(sys.path)
    located = find_executables()
    versions_info["ffmpeg"] = located["ffmpeg"]
    versions_info["ffprobe"] = located["ffprobe"]
    return json.dumps(versions_info, ensure_ascii=True, separators=(",", ":"))
