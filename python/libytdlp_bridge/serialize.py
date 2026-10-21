import json

from yt_dlp import YoutubeDL


def dumps(info):
    cleaned = YoutubeDL.sanitize_info(info)
    return json.dumps(cleaned, ensure_ascii=True, separators=(",", ":"))
