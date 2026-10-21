use std::ffi::c_char;
use std::ptr;

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
        struct Guard(*mut sys::LyResult);

        impl Drop for Guard {
            fn drop(&mut self) {
                unsafe {
                    sys::ly_result_release(self.0);
                }
            }
        }

        let guard = Guard(result);

        let mut data: *const c_char = ptr::null();
        let mut len: usize = 0;

        let status = unsafe { sys::ly_result_json(guard.0, &mut data, &mut len) };

        if status != sys::LY_OK || data.is_null() {
            return Err(Error::new(Kind::Internal, "could not read result JSON"));
        }

        let bytes = unsafe { std::slice::from_raw_parts(data.cast::<u8>(), len) };

        let text = std::str::from_utf8(bytes)
            .map_err(|_| Error::new(Kind::Internal, "result JSON is not valid UTF-8"))?;

        let value: serde_json::Value = serde_json::from_str(text)
            .map_err(|failed| Error::new(Kind::Internal, failed.to_string()))?;

        Ok(Self {
            raw: text.to_owned(),
            value,
        })
    }
}
