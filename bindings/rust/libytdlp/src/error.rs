use std::ffi::CStr;
use std::fmt;

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
}

impl Error {
    pub fn kind(&self) -> Kind {
        self.kind
    }

    pub fn message(&self) -> &str {
        &self.message
    }

    pub(crate) fn new(kind: Kind, message: impl Into<String>) -> Self {
        Self {
            kind,
            message: message.into(),
        }
    }

    pub(crate) fn from_ptr(error: *mut sys::LyError) -> Self {
        if error.is_null() {
            return Self::new(Kind::Internal, "libytdlp reported failure without detail");
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

        unsafe {
            sys::ly_error_release(error);
        }

        let kind = Kind::from_status(code);

        if text.is_empty() {
            return Self::new(kind, format!("libytdlp error: {}", kind.as_str()));
        }

        Self::new(kind, text)
    }
}

impl fmt::Display for Error {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(formatter, "{}: {}", self.kind.as_str(), self.message)
    }
}

impl std::error::Error for Error {}
