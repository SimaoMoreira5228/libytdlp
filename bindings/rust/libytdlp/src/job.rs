use std::future::Future;
use std::pin::Pin;
use std::ptr;
use std::sync::Arc;
use std::sync::Mutex;
use std::task::Context;
use std::task::Poll;
use std::task::Waker;

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

    #[tracing::instrument(name = "libytdlp.poll", level = "debug", skip(self))]
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
        let values: Vec<ProgressEvent> =
            values.into_iter().map(ProgressEvent::from_value).collect();

        tracing::debug!(events = values.len(), "libytdlp poll drained");

        Ok((state, values))
    }

    #[tracing::instrument(name = "libytdlp.wait", level = "debug", skip(self))]
    pub fn wait(&self) -> Result<MediaInfo, Error> {
        let mut result: *mut sys::LyResult = ptr::null_mut();
        let mut error: *mut sys::LyError = ptr::null_mut();

        let status = unsafe { sys::ly_job_wait(self.raw, &mut result, &mut error) };

        if status != sys::LY_OK || result.is_null() {
            return Err(Error::from_ptr(error));
        }

        MediaInfo::from_raw(result)
    }

    pub fn wait_async(self) -> WaitForDownload {
        WaitForDownload {
            job: Some(self),
            shared: Arc::new(Shared {
                waker: Mutex::new(None),
                outcome: Mutex::new(None),
            }),
        }
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

struct Shared {
    waker: Mutex<Option<Waker>>,
    outcome: Mutex<Option<Result<MediaInfo, Error>>>,
}

pub struct WaitForDownload {
    job: Option<DownloadJob>,
    shared: Arc<Shared>,
}

impl Future for WaitForDownload {
    type Output = Result<MediaInfo, Error>;

    fn poll(mut self: Pin<&mut Self>, context: &mut Context<'_>) -> Poll<Self::Output> {
        if let Some(outcome) = self.shared.outcome.lock().expect("outcome lock").take() {
            return Poll::Ready(outcome);
        }

        *self.shared.waker.lock().expect("waker lock") = Some(context.waker().clone());

        if let Some(outcome) = self.shared.outcome.lock().expect("outcome lock").take() {
            return Poll::Ready(outcome);
        }

        let Some(job) = self.job.take() else {
            return Poll::Pending;
        };

        let shared = Arc::clone(&self.shared);

        std::thread::spawn(move || {
            let outcome = job.wait();

            *shared.outcome.lock().expect("outcome lock") = Some(outcome);

            let waker = shared.waker.lock().expect("waker lock").take();

            if let Some(waker) = waker {
                waker.wake();
            }
        });

        Poll::Pending
    }
}
