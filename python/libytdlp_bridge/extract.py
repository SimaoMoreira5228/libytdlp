import json

import yt_dlp
from yt_dlp import YoutubeDL

from .serialize import dumps

BRIDGE_VERSION = "0.1.0"


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


def extract_json(url, options_json="{}"):
    if not url or not isinstance(url, str):
        raise ValueError("url must be a non-empty string")

    options = json.loads(options_json) if options_json else {}

    with YoutubeDL(_params(options)) as ydl:
        info = ydl.extract_info(url, download=False)

    return dumps(info)


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
    versions_info = json.loads(versions())
    versions_info["wrapper"] = BRIDGE_VERSION
    versions_info["backend"] = "embedded"
    versions_info["deployment_profile"] = profile
    versions_info["ytdlp_commit"] = ytdlp_commit
    versions_info["abi"] = int(abi)
    versions_info["host_owned"] = bool(host_owned)
    return json.dumps(versions_info, ensure_ascii=True, separators=(",", ":"))
