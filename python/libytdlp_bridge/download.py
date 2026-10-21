import json

import yt_dlp
from yt_dlp import YoutubeDL

from .serialize import dumps


class _Cancelled(Exception):
    pass


class _SilentLogger:
    def debug(self, message):
        pass

    def warning(self, message):
        pass

    def error(self, message):
        pass


def _load_options(text):
    if not text:
        return {}

    parsed = json.loads(text)

    if not isinstance(parsed, dict):
        raise ValueError("options must be a JSON object")

    return parsed


def _merge_options(context_options, job_options):
    merged = _load_options(context_options)
    overrides = _load_options(job_options)
    merged.update(overrides)

    return merged


def _params(options, events, cancel_event):
    def hook(progress):
        if cancel_event.is_set():
            raise _Cancelled("cancelled")

        events.append(json.dumps(progress, ensure_ascii=True, separators=(",", ":"), default=repr))

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

    params["progress_hooks"] = [*params.get("progress_hooks", []), hook]

    return params


def download_json(
    url,
    context_options_json="{}",
    job_options_json="{}",
    events=None,
    cancel_event=None,
):
    if not url or not isinstance(url, str):
        raise ValueError("url must be a non-empty string")

    if events is None:
        raise ValueError("events must be a list")

    if cancel_event is None:
        raise ValueError("cancel_event must be a threading.Event")

    options = _merge_options(context_options_json, job_options_json)

    with YoutubeDL(_params(options, events, cancel_event)) as ydl:
        info = ydl.extract_info(url, download=True)

    return dumps(info)
