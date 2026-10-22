use std::ffi::CStr;
use std::fmt;
use std::ptr;

use libytdlp_sys as sys;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Kind {
    InvalidArgument,
    Runtime,
    Extract,
    Network,
    Cancelled,
    Busy,
    Internal,
}

impl Kind {
    fn from_status(status: u32) -> Self {
        match status {
            sys::LY_E_INVALID_ARGUMENT => Self::InvalidArgument,
            sys::LY_E_RUNTIME => Self::Runtime,
            sys::LY_E_EXTRACT => Self::Extract,
            sys::LY_E_NETWORK => Self::Network,
            sys::LY_E_CANCELLED => Self::Cancelled,
            sys::LY_E_BUSY => Self::Busy,
            _ => Self::Internal,
        }
    }

    fn as_str(self) -> &'static str {
        match self {
            Self::InvalidArgument => "invalid argument",
            Self::Runtime => "runtime",
            Self::Extract => "extract",
            Self::Network => "network",
            Self::Cancelled => "cancelled",
            Self::Busy => "busy",
            Self::Internal => "internal",
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Error {
    kind: Kind,
    message: String,
    exception_class: Option<String>,
    retryable: bool,
}

impl Error {
    pub fn kind(&self) -> Kind {
        self.kind
    }

    pub fn message(&self) -> &str {
        &self.message
    }

    pub fn exception_class(&self) -> Option<&str> {
        self.exception_class.as_deref()
    }

    pub fn is_retryable(&self) -> bool {
        self.retryable
    }

    pub(crate) fn log(&self) {
        tracing::debug!(kind = self.kind.as_str(), "libytdlp call failed");
    }

    pub(crate) fn new(kind: Kind, message: impl Into<String>) -> Self {
        Self {
            kind,
            message: message.into(),
            exception_class: None,
            retryable: false,
        }
    }

    fn with_class(mut self, exception_class: Option<String>) -> Self {
        self.exception_class = exception_class;

        self
    }

    fn read_exception_class(error: *mut sys::LyError) -> Option<String> {
        let mut data: *const std::ffi::c_char = ptr::null();
        let mut len: usize = 0;

        let status = unsafe { sys::ly_error_details_json(error, &mut data, &mut len) };

        if status != sys::LY_OK || data.is_null() {
            return None;
        }

        let bytes = unsafe { std::slice::from_raw_parts(data.cast::<u8>(), len) };

        let text = std::str::from_utf8(bytes).ok()?;

        let value: serde_json::Value = serde_json::from_str(text).ok()?;

        value
            .get("class")
            .and_then(|class| class.as_str())
            .map(str::to_owned)
    }

    pub(crate) fn from_ptr(error: *mut sys::LyError) -> Self {
        if error.is_null() {
            let failure = Self::new(Kind::Internal, "libytdlp reported failure without detail");
            failure.log();
            return failure;
        }

        let code = unsafe { sys::ly_error_code(error) };

        let text = unsafe {
            let raw = sys::ly_error_message(error);

            if raw.is_null() {
                String::new()
            } else {
                CStr::from_ptr(raw).to_string_lossy().into_owned()
            }
        };

        let exception_class = Self::read_exception_class(error);
        let retryable = unsafe { sys::ly_error_is_retryable(error) };

        unsafe {
            sys::ly_error_release(error);
        }

        let kind = Kind::from_status(code);

        let failure = if text.is_empty() {
            Self::new(kind, format!("libytdlp error: {}", kind.as_str()))
                .with_class(exception_class)
        } else {
            Self::new(kind, text).with_class(exception_class)
        };

        let failure = Self {
            retryable,
            ..failure
        };
        failure.log();

        failure
    }
}

impl fmt::Display for Error {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(formatter, "{}: {}", self.kind.as_str(), self.message)
    }
}

impl std::error::Error for Error {}
