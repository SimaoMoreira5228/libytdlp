import json
import os

import yt_dlp
from yt_dlp import YoutubeDL

from .options import merge_options
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


def _contains(root, candidate):
    try:
        resolved = os.path.realpath(candidate)
    except (OSError, ValueError):
        return False

    try:
        common = os.path.commonpath((root, resolved))
    except ValueError:
        return False

    return common == root


def _resolve_root(value):
    if not isinstance(value, str):
        raise ValueError("libytdlp.restrict_to must be a string")

    if not value:
        raise ValueError("libytdlp.restrict_to must not be empty")

    if not os.path.isabs(value):
        raise ValueError("libytdlp.restrict_to must be an absolute path")

    return os.path.realpath(value)


def _confine_template_path(root, template):
    if not isinstance(template, str):
        raise ValueError("confined outtmpl must be a string")

    if not template:
        raise ValueError("confined outtmpl must not be empty")

    parent = os.path.dirname(template)

    if not parent:
        parent = "."

    if not _contains(root, parent):
        raise ValueError("outtmpl escapes the restricted root")


def _confine_outtmpl(root, value):
    if isinstance(value, str):
        _confine_template_path(root, value)

        return

    if isinstance(value, dict):
        for variant in value.values():
            _confine_template_path(root, variant)

        return

    raise ValueError("confined outtmpl must be a string or a dict")


def _confine_paths(root, value):
    if not isinstance(value, dict):
        raise ValueError("confined paths must be an object")

    for entry in value.values():
        if not isinstance(entry, str):
            raise ValueError("confined paths entries must be strings")

        if not entry:
            raise ValueError("confined paths entries must not be empty")

        if not _contains(root, entry):
            raise ValueError("paths entry escapes the restricted root")


def _enforce_restriction(staged):
    if "libytdlp" not in staged:
        return

    raw = staged.pop("libytdlp")

    if not isinstance(raw, dict):
        raise ValueError("libytdlp options must be an object")

    root = _resolve_root(raw.get("restrict_to"))

    if "outtmpl" not in staged and "paths" not in staged:
        raise ValueError("restricted downloads need an explicit outtmpl or paths")

    if "outtmpl" in staged:
        _confine_outtmpl(root, staged["outtmpl"])

    if "paths" in staged:
        _confine_paths(root, staged["paths"])


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

        staged = dict(options)

        _enforce_restriction(staged)

        params.update(staged)

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

    options = merge_options(context_options_json, job_options_json)

    with YoutubeDL(_params(options, events, cancel_event)) as ydl:
        info = ydl.extract_info(url, download=True)

    return dumps(info)
