#[derive(Debug, Clone)]
pub struct ProgressEvent {
    value: serde_json::Value,
}

impl ProgressEvent {
    pub fn status(&self) -> Option<&str> {
        self.value.get("status")?.as_str()
    }

    pub fn downloaded_bytes(&self) -> Option<u64> {
        self.value.get("downloaded_bytes")?.as_u64()
    }

    pub fn total_bytes(&self) -> Option<u64> {
        self.value
            .get("total_bytes")
            .or_else(|| self.value.get("total_bytes_estimate"))?
            .as_u64()
    }

    pub fn filename(&self) -> Option<&str> {
        self.value.get("filename")?.as_str()
    }

    pub fn value(&self) -> &serde_json::Value {
        &self.value
    }

    pub(crate) fn from_value(value: serde_json::Value) -> Self {
        Self { value }
    }
}
