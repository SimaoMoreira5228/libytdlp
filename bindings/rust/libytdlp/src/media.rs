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

        let value: serde_json::Value = serde_json::from_str(&text)
            .map_err(|failed| Error::new(Kind::Internal, failed.to_string()))?;

        Ok(Self { raw: text, value })
    }
}
