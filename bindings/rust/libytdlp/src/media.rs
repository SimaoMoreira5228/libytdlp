use libytdlp_sys as sys;

use crate::error::Error;
use crate::error::Kind;

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
        self.value.get("title")?.as_str()
    }

    pub fn id(&self) -> Option<&str> {
        self.value.get("id")?.as_str()
    }

    pub(crate) fn from_raw(result: *mut sys::LyResult) -> Result<Self, Error> {
        let text = crate::read_result_json(result)?;

        let value: serde_json::Value = serde_json::from_str(&text)
            .map_err(|failed| Error::new(Kind::Internal, failed.to_string()))?;

        Ok(Self { raw: text, value })
    }
}
