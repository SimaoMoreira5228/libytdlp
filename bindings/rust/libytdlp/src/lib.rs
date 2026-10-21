use std::ffi::c_char;
use std::ptr;

use libytdlp_sys as sys;

mod client;
mod error;
mod job;
mod media;
mod progress;
mod runtime;

pub use client::Client;
pub use client::ClientBuilder;
pub use error::{Error, Kind};
pub use job::{DownloadJob, JobState, WaitForDownload};
pub use media::Format;
pub use media::MediaEntry;
pub use media::MediaInfo;
pub use progress::ProgressEvent;
pub use runtime::{RuntimeInfo, abi_version, runtime_info};

pub(crate) fn read_result_json(result: *mut sys::LyResult) -> Result<String, error::Error> {
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
        return Err(error::Error::new(
            error::Kind::Internal,
            "could not read result JSON",
        ));
    }

    let bytes = unsafe { std::slice::from_raw_parts(data.cast::<u8>(), len) };

    std::str::from_utf8(bytes)
        .map(str::to_owned)
        .map_err(|_| error::Error::new(error::Kind::Internal, "result JSON is not valid UTF-8"))
}
