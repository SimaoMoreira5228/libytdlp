use std::ffi::c_char;
use std::ffi::c_uint;

pub enum LyContext {}

pub enum LyResult {}

pub enum LyError {}

pub enum LyJob {}

pub const LY_JOB_RUNNING: c_uint = 0;
pub const LY_JOB_DONE: c_uint = 1;
pub const LY_JOB_FAILED: c_uint = 2;
pub const LY_JOB_CANCELLED: c_uint = 3;

pub const LY_OK: c_uint = 0;
pub const LY_E_INVALID_ARGUMENT: c_uint = 1;
pub const LY_E_RUNTIME: c_uint = 2;
pub const LY_E_EXTRACT: c_uint = 3;
pub const LY_E_NETWORK: c_uint = 4;
pub const LY_E_CANCELLED: c_uint = 5;
pub const LY_E_BUSY: c_uint = 6;
pub const LY_E_INTERNAL: c_uint = 7;

unsafe extern "C" {
    pub fn ly_abi_version() -> u32;

    pub fn ly_context_create(
        options_json: *const c_char,
        options_len: usize,
        out: *mut *mut LyContext,
        error: *mut *mut LyError,
    ) -> c_uint;

    pub fn ly_context_release(context: *mut LyContext) -> c_uint;

    pub fn ly_extract(
        context: *mut LyContext,
        url: *const c_char,
        url_len: usize,
        out: *mut *mut LyResult,
        error: *mut *mut LyError,
    ) -> c_uint;

    pub fn ly_result_json(
        result: *const LyResult,
        data: *mut *const c_char,
        len: *mut usize,
    ) -> c_uint;

    pub fn ly_result_release(result: *mut LyResult);

    pub fn ly_version_info(out: *mut *mut LyResult, error: *mut *mut LyError) -> c_uint;

    pub fn ly_error_code(error: *const LyError) -> c_uint;

    pub fn ly_error_message(error: *const LyError) -> *const c_char;

    pub fn ly_error_details_json(
        error: *const LyError,
        data: *mut *const c_char,
        len: *mut usize,
    ) -> c_uint;

    pub fn ly_download_start(
        context: *mut LyContext,
        url: *const c_char,
        url_len: usize,
        options_json: *const c_char,
        options_len: usize,
        out: *mut *mut LyJob,
        error: *mut *mut LyError,
    ) -> c_uint;

    pub fn ly_job_poll(
        job: *mut LyJob,
        state: *mut c_uint,
        events: *mut *mut LyResult,
        error: *mut *mut LyError,
    ) -> c_uint;

    pub fn ly_job_wait(
        job: *mut LyJob,
        out: *mut *mut LyResult,
        error: *mut *mut LyError,
    ) -> c_uint;

    pub fn ly_job_cancel(job: *mut LyJob) -> c_uint;

    pub fn ly_job_release(job: *mut LyJob);

    pub fn ly_error_release(error: *mut LyError);
}
