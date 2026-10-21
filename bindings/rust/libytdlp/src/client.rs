use std::ptr;

use libytdlp_sys as sys;

use crate::error::Error;
use crate::error::Kind;
use crate::job::DownloadJob;
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

    #[tracing::instrument(name = "libytdlp.extract", level = "debug", skip(self, url))]
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

    #[tracing::instrument(name = "libytdlp.download", level = "debug", skip(self, url, options))]
    pub fn download(&self, url: &str, options: &str) -> Result<DownloadJob, Error> {
        if url.as_bytes().contains(&0) {
            return Err(Error::new(
                Kind::InvalidArgument,
                "url must not contain NUL bytes",
            ));
        }

        if options.as_bytes().contains(&0) {
            return Err(Error::new(
                Kind::InvalidArgument,
                "options must not contain NUL bytes",
            ));
        }

        let mut job: *mut sys::LyJob = ptr::null_mut();
        let mut error: *mut sys::LyError = ptr::null_mut();

        let status = unsafe {
            sys::ly_download_start(
                self.raw,
                url.as_ptr().cast(),
                url.len(),
                options.as_ptr().cast(),
                options.len(),
                &mut job,
                &mut error,
            )
        };

        if status != sys::LY_OK || job.is_null() {
            return Err(Error::from_ptr(error));
        }

        Ok(DownloadJob::from_raw(job))
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
