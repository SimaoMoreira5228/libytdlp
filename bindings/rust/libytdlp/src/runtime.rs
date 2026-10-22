use std::ptr;

use libytdlp_sys as sys;

use crate::error::Error;
use crate::error::Kind;

#[derive(Debug)]
pub struct RuntimeInfo {
    raw: String,
    value: serde_json::Value,
}

impl RuntimeInfo {
    pub fn raw_json(&self) -> &str {
        &self.raw
    }

    pub fn value(&self) -> &serde_json::Value {
        &self.value
    }

    pub fn abi(&self) -> u32 {
        self.value
            .get("abi")
            .and_then(serde_json::Value::as_u64)
            .unwrap_or(0) as u32
    }

    pub fn wrapper(&self) -> &str {
        self.text("wrapper")
    }

    pub fn backend(&self) -> &str {
        self.text("backend")
    }

    pub fn profile(&self) -> &str {
        self.text("deployment_profile")
    }

    pub fn python(&self) -> &str {
        self.text("python")
    }

    pub fn ytdlp_version(&self) -> &str {
        self.text("yt_dlp")
    }

    pub fn ytdlp_commit(&self) -> &str {
        self.text("ytdlp_commit")
    }

    pub fn host_owned(&self) -> bool {
        self.value
            .get("host_owned")
            .and_then(serde_json::Value::as_bool)
            .unwrap_or(false)
    }

    pub fn ffmpeg(&self) -> Option<&str> {
        self.value.get("ffmpeg")?.as_str()
    }

    pub fn ffprobe(&self) -> Option<&str> {
        self.value.get("ffprobe")?.as_str()
    }

    fn text(&self, key: &str) -> &str {
        self.value
            .get(key)
            .and_then(serde_json::Value::as_str)
            .unwrap_or("")
    }
}

pub fn abi_version() -> u32 {
    unsafe { sys::ly_abi_version() }
}

pub(crate) fn ensure_compatible_abi() -> Result<(), Error> {
    let linked = abi_version();

    if linked != sys::LY_ABI_VERSION {
        return Err(Error::new(
            Kind::Internal,
            format!(
                "native libytdlp ABI {linked} does not match bindings ABI {}",
                sys::LY_ABI_VERSION,
            ),
        ));
    }

    Ok(())
}

#[tracing::instrument(name = "libytdlp.runtime_info", level = "debug")]
pub fn runtime_info() -> Result<RuntimeInfo, Error> {
    let mut result: *mut sys::LyResult = ptr::null_mut();
    let mut error: *mut sys::LyError = ptr::null_mut();

    let status = unsafe { sys::ly_version_info(&mut result, &mut error) };

    if status != sys::LY_OK || result.is_null() {
        return Err(Error::from_ptr(error));
    }

    let text = crate::read_result_json(result)?;

    let value: serde_json::Value = serde_json::from_str(&text)
        .map_err(|failed| Error::new(Kind::Internal, failed.to_string()))?;

    Ok(RuntimeInfo { raw: text, value })
}
