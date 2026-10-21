use std::ptr;

use libytdlp_sys as sys;

use crate::error::Error;
use crate::error::Kind;
use crate::media::MediaInfo;

pub struct Client {
    raw: *mut sys::LyContext,
}

impl Client {
    pub fn new(options_json: &str) -> Result<Self, Error> {
        let mut context: *mut sys::LyContext = ptr::null_mut();
        let mut error: *mut sys::LyError = ptr::null_mut();

        let status = unsafe {
            sys::ly_context_create(
                options_json.as_ptr().cast(),
                options_json.len(),
                &mut context,
                &mut error,
            )
        };

        if status != sys::LY_OK || context.is_null() {
            return Err(Error::from_ptr(error));
        }

        Ok(Self { raw: context })
    }

    pub fn extract(&self, url: &str) -> Result<MediaInfo, Error> {
        if url.as_bytes().contains(&0) {
            return Err(Error::new(
                Kind::InvalidArgument,
                "url must not contain NUL bytes",
            ));
        }

        let mut result: *mut sys::LyResult = ptr::null_mut();
        let mut error: *mut sys::LyError = ptr::null_mut();

        let status = unsafe {
            sys::ly_extract(
                self.raw,
                url.as_ptr().cast(),
                url.len(),
                &mut result,
                &mut error,
            )
        };

        if status != sys::LY_OK || result.is_null() {
            return Err(Error::from_ptr(error));
        }

        MediaInfo::from_raw(result)
    }
}

impl Drop for Client {
    fn drop(&mut self) {
        unsafe {
            sys::ly_context_release(self.raw);
        }
    }
}

unsafe impl Send for Client {}

unsafe impl Sync for Client {}
