use libytdlp_sys as sys;

use crate::error::Error;
use crate::error::Kind;

#[derive(Debug, Clone)]
pub struct Format {
    value: serde_json::Value,
}

fn field<'v>(value: &'v serde_json::Value, key: &str) -> Option<&'v str> {
    value.get(key)?.as_str()
}

fn seconds(value: &serde_json::Value) -> Option<f64> {
    let duration = value.get("duration")?;

    duration
        .as_f64()
        .or_else(|| duration.as_u64().map(|duration| duration as f64))
}

impl Format {
    pub fn format_id(&self) -> Option<&str> {
        field(&self.value, "format_id")
    }

    pub fn url(&self) -> Option<&str> {
        field(&self.value, "url")
    }

    pub fn ext(&self) -> Option<&str> {
        field(&self.value, "ext")
    }

    pub fn acodec(&self) -> Option<&str> {
        field(&self.value, "acodec")
    }

    pub fn vcodec(&self) -> Option<&str> {
        self.value.get("vcodec")?.as_str()
    }

    pub fn asr(&self) -> Option<u64> {
        self.value.get("asr")?.as_u64()
    }

    pub fn abr(&self) -> Option<f64> {
        let rate = self.value.get("abr")?;
        rate.as_f64()
            .or_else(|| rate.as_u64().map(|rate| rate as f64))
    }

    pub fn filesize(&self) -> Option<u64> {
        self.value
            .get("filesize")
            .or_else(|| self.value.get("filesize_approx"))?
            .as_u64()
    }

    pub fn is_audio_only(&self) -> bool {
        let audio = self.value.get("acodec").and_then(serde_json::Value::as_str);

        let missing_audio = audio.is_none_or(|codec| codec == "none");

        if missing_audio {
            return false;
        }

        let video = self.value.get("vcodec").and_then(serde_json::Value::as_str);

        video.is_none_or(|codec| codec == "none")
    }

    pub fn value(&self) -> &serde_json::Value {
        &self.value
    }

    pub(crate) fn from_value(value: serde_json::Value) -> Self {
        Self { value }
    }
}

#[derive(Debug, Clone)]
pub struct MediaEntry {
    value: serde_json::Value,
}

impl MediaEntry {
    pub fn id(&self) -> Option<&str> {
        self.value.get("id")?.as_str()
    }

    pub fn title(&self) -> Option<&str> {
        self.value.get("title")?.as_str()
    }

    pub fn url(&self) -> Option<&str> {
        self.value.get("url")?.as_str()
    }

    pub fn webpage_url(&self) -> Option<&str> {
        self.value.get("webpage_url")?.as_str()
    }

    pub fn duration_secs(&self) -> Option<f64> {
        seconds(&self.value)
    }

    pub fn uploader(&self) -> Option<&str> {
        self.value.get("uploader")?.as_str()
    }

    pub fn channel(&self) -> Option<&str> {
        self.value.get("channel")?.as_str()
    }

    pub fn thumbnail(&self) -> Option<&str> {
        self.value.get("thumbnail")?.as_str()
    }

    pub fn thumbnails(&self) -> Vec<&str> {
        self.value
            .get("thumbnails")
            .and_then(serde_json::Value::as_array)
            .map(|items| {
                items
                    .iter()
                    .filter_map(|item| item.get("url")?.as_str())
                    .collect()
            })
            .unwrap_or_default()
    }

    pub fn value(&self) -> &serde_json::Value {
        &self.value
    }

    pub(crate) fn from_value(value: serde_json::Value) -> Self {
        Self { value }
    }
}

#[derive(Debug)]
pub struct MediaInfo {
    raw: String,
    value: serde_json::Value,
}

impl MediaInfo {
    pub fn raw_json(&self) -> &str {
        &self.raw
    }

    pub fn value(&self) -> &serde_json::Value {
        &self.value
    }

    pub fn title(&self) -> Option<&str> {
        field(&self.value, "title")
    }

    pub fn id(&self) -> Option<&str> {
        field(&self.value, "id")
    }

    pub fn duration_secs(&self) -> Option<f64> {
        seconds(&self.value)
    }

    pub fn uploader(&self) -> Option<&str> {
        field(&self.value, "uploader")
    }

    pub fn channel(&self) -> Option<&str> {
        field(&self.value, "channel")
    }

    pub fn thumbnail(&self) -> Option<&str> {
        field(&self.value, "thumbnail")
    }

    pub fn extractor(&self) -> Option<&str> {
        field(&self.value, "extractor")
    }

    pub fn is_live(&self) -> Option<bool> {
        self.value.get("is_live")?.as_bool()
    }

    pub fn live_status(&self) -> Option<&str> {
        field(&self.value, "live_status")
    }

    pub fn is_playlist(&self) -> bool {
        self.value
            .get("_type")
            .and_then(serde_json::Value::as_str)
            .is_some_and(|kind| kind == "playlist")
    }

    pub fn playlist_count(&self) -> Option<u64> {
        self.value.get("playlist_count")?.as_u64()
    }

    pub fn webpage_url(&self) -> Option<&str> {
        field(&self.value, "webpage_url")
    }

    pub fn formats(&self) -> Vec<Format> {
        self.value
            .get("formats")
            .and_then(serde_json::Value::as_array)
            .map(|formats| formats.iter().cloned().map(Format::from_value).collect())
            .unwrap_or_default()
    }

    pub fn best_audio_url(&self) -> Option<&str> {
        let formats = self.value.get("formats")?.as_array()?;

        let mut best_url: Option<&str> = None;

        let mut best_rate = 0.0;

        let mut found = false;

        for format in formats {
            let audio = format.get("acodec").and_then(serde_json::Value::as_str);

            if audio.is_none_or(|codec| codec == "none") {
                continue;
            }

            let video = format.get("vcodec").and_then(serde_json::Value::as_str);

            if video.is_some_and(|codec| codec != "none") {
                continue;
            }

            let rate = format
                .get("abr")
                .and_then(|rate| {
                    rate.as_f64()
                        .or_else(|| rate.as_u64().map(|rate| rate as f64))
                })
                .unwrap_or(0.0);

            if found && rate <= best_rate {
                continue;
            }

            let url = format.get("url")?.as_str()?;

            best_url = Some(url);

            best_rate = rate;

            found = true;
        }

        best_url
    }

    pub fn entries(&self) -> Vec<MediaEntry> {
        self.value
            .get("entries")
            .and_then(serde_json::Value::as_array)
            .map(|entries| {
                entries
                    .iter()
                    .cloned()
                    .map(MediaEntry::from_value)
                    .collect()
            })
            .unwrap_or_default()
    }

    pub(crate) fn from_raw(result: *mut sys::LyResult) -> Result<Self, Error> {
        let text = crate::read_result_json(result)?;

        Self::from_json_text(text)
    }

    pub(crate) fn from_json_text(raw: String) -> Result<Self, Error> {
        let value: serde_json::Value = serde_json::from_str(&raw)
            .map_err(|failed| Error::new(Kind::Internal, failed.to_string()))?;

        Ok(Self { raw, value })
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn info(value: serde_json::Value) -> MediaInfo {
        let raw = serde_json::to_string(&value).expect("fixture must serialize");

        MediaInfo { raw, value }
    }

    fn format(value: serde_json::Value) -> Format {
        Format::from_value(value)
    }

    #[test]
    fn audio_only_matches_codecs() {
        let audio = format(serde_json::json!({"acodec": "opus", "vcodec": "none"}));

        assert!(audio.is_audio_only());

        let missing_video = format(serde_json::json!({"acodec": "opus"}));

        assert!(missing_video.is_audio_only());

        let video = format(serde_json::json!({"acodec": "opus", "vcodec": "avc1"}));

        assert!(!video.is_audio_only());

        let no_audio = format(serde_json::json!({"acodec": "none", "vcodec": "none"}));

        assert!(!no_audio.is_audio_only());

        let missing_audio = format(serde_json::json!({"vcodec": "none"}));

        assert!(!missing_audio.is_audio_only());

        let empty = format(serde_json::json!({}));

        assert!(!empty.is_audio_only());
    }

    #[test]
    fn best_audio_url_prefers_highest_rate() {
        let media = info(serde_json::json!({
            "formats": [
                {"acodec": "opus", "vcodec": "none", "abr": 48.0, "url": "low"},
                {"acodec": "opus", "vcodec": "avc1", "abr": 999.0, "url": "video"},
                {"acodec": "opus", "vcodec": "none", "abr": 160.0, "url": "high"},
                {"acodec": "opus", "abr": 200.0, "url": "top"},
            ],
        }));

        assert_eq!(media.best_audio_url(), Some("top"));
    }

    #[test]
    fn best_audio_url_handles_missing_rates_and_ties() {
        let media = info(serde_json::json!({
            "formats": [
                {"acodec": "opus", "vcodec": "none", "url": "first"},
                {"acodec": "opus", "vcodec": "none", "url": "second"},
            ],
        }));

        assert_eq!(media.best_audio_url(), Some("first"));

        let rated = info(serde_json::json!({
            "formats": [
                {"acodec": "opus", "vcodec": "none", "url": "unrated"},
                {"acodec": "opus", "vcodec": "none", "abr": 64, "url": "rated"},
            ],
        }));

        assert_eq!(rated.best_audio_url(), Some("rated"));
    }

    #[test]
    fn best_audio_url_reports_none_without_audio() {
        let media = info(serde_json::json!({
            "formats": [
                {"acodec": "none", "vcodec": "avc1", "url": "video"},
            ],
        }));

        assert_eq!(media.best_audio_url(), None);

        let missing = info(serde_json::json!({"id": "x"}));

        assert_eq!(missing.best_audio_url(), None);
    }

    #[test]
    fn entries_collect_mixed_items() {
        let media = info(serde_json::json!({
            "_type": "playlist",
            "entries": [
                {"id": "a", "title": "first"},
                {"id": "b"},
                "stray",
            ],
        }));

        let entries = media.entries();

        assert_eq!(entries.len(), 3);
        assert_eq!(entries[0].id(), Some("a"));
        assert_eq!(entries[0].title(), Some("first"));
        assert_eq!(entries[1].id(), Some("b"));
        assert_eq!(entries[1].title(), None);
        assert_eq!(entries[2].id(), None);
    }

    #[test]
    fn playlist_flags_match_literal_type() {
        let playlist = info(serde_json::json!({"_type": "playlist", "playlist_count": 3}));

        assert!(playlist.is_playlist());
        assert_eq!(playlist.playlist_count(), Some(3));

        let video = info(serde_json::json!({"_type": "video"}));

        assert!(!video.is_playlist());
        assert_eq!(video.playlist_count(), None);

        let ambiguous = info(serde_json::json!({"entries": [{"id": "a"}]}));

        assert!(!ambiguous.is_playlist());
        assert!(!ambiguous.entries().is_empty());
    }

    #[test]
    fn live_flags_read_optional_fields() {
        let live = info(serde_json::json!({"is_live": true, "live_status": "is_live"}));

        assert_eq!(live.is_live(), Some(true));
        assert_eq!(live.live_status(), Some("is_live"));

        let plain = info(serde_json::json!({}));

        assert_eq!(plain.is_live(), None);
        assert_eq!(plain.live_status(), None);
    }

    #[test]
    fn entry_accessors_read_sparse_objects() {
        let entry = MediaEntry::from_value(serde_json::json!({
            "id": "a",
            "uploader": "sam",
            "thumbnails": [
                {"url": "thumb-a"},
                {"id": "no-url"},
                "stray",
            ],
        }));

        assert_eq!(entry.uploader(), Some("sam"));
        assert_eq!(entry.channel(), None);
        assert_eq!(entry.thumbnail(), None);
        assert_eq!(entry.thumbnails(), ["thumb-a"]);

        let sparse = MediaEntry::from_value(serde_json::json!({"id": "b"}));

        assert_eq!(sparse.uploader(), None);
        assert_eq!(sparse.channel(), None);
        assert_eq!(sparse.thumbnail(), None);
        assert!(sparse.thumbnails().is_empty());
    }
}
