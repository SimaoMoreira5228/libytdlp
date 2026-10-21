import json


def load_options(text):
    if not text:
        return {}

    parsed = json.loads(text)

    if not isinstance(parsed, dict):
        raise ValueError("options must be a JSON object")

    return parsed


def merge_options(context_options, call_options):
    merged = load_options(context_options)
    overrides = load_options(call_options)
    merged.update(overrides)

    return merged
