use std::ptr;

use libytdlp_sys as sys;

use crate::error::Error;
use crate::error::Kind;
use crate::media::MediaInfo;
use crate::progress::ProgressEvent;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum JobState {
    Running,
    Done,
    Failed,
    Cancelled,
}

impl JobState {
    fn from_raw(state: u32) -> Result<Self, Error> {
        match state {
            sys::LY_JOB_RUNNING => Ok(Self::Running),
            sys::LY_JOB_DONE => Ok(Self::Done),
            sys::LY_JOB_FAILED => Ok(Self::Failed),
            sys::LY_JOB_CANCELLED => Ok(Self::Cancelled),
            _ => Err(Error::new(Kind::Internal, "unknown job state")),
        }
    }
}

pub struct DownloadJob {
    raw: *mut sys::LyJob,
}

impl DownloadJob {
    pub(crate) fn from_raw(raw: *mut sys::LyJob) -> Self {
        Self { raw }
    }

    pub fn poll(&self) -> Result<(JobState, Vec<ProgressEvent>), Error> {
        let mut state: u32 = sys::LY_JOB_RUNNING;
        let mut events: *mut sys::LyResult = ptr::null_mut();
        let mut error: *mut sys::LyError = ptr::null_mut();

        let status = unsafe { sys::ly_job_poll(self.raw, &mut state, &mut events, &mut error) };

        if status != sys::LY_OK || events.is_null() {
            return Err(Error::from_ptr(error));
        }

        let text = crate::read_result_json(events)?;

        let state = JobState::from_raw(state)?;

        let values: Vec<serde_json::Value> = serde_json::from_str(&text)
            .map_err(|failed| Error::new(Kind::Internal, failed.to_string()))?;

        Ok((
            state,
            values.into_iter().map(ProgressEvent::from_value).collect(),
        ))
    }

    pub fn wait(&self) -> Result<MediaInfo, Error> {
        let mut result: *mut sys::LyResult = ptr::null_mut();
        let mut error: *mut sys::LyError = ptr::null_mut();

        let status = unsafe { sys::ly_job_wait(self.raw, &mut result, &mut error) };

        if status != sys::LY_OK || result.is_null() {
            return Err(Error::from_ptr(error));
        }

        MediaInfo::from_raw(result)
    }

    pub fn cancel(&self) {
        unsafe {
            sys::ly_job_cancel(self.raw);
        }
    }
}

impl Drop for DownloadJob {
    fn drop(&mut self) {
        unsafe {
            sys::ly_job_release(self.raw);
        }
    }
}

unsafe impl Send for DownloadJob {}

unsafe impl Sync for DownloadJob {}
