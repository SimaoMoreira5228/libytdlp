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
use crate::job::DownloadJob;
use crate::media::MediaInfo;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum PlayerClient {
    Default,
    Android,
    TvEmbedded,
    Ios,
    Web,
}

impl PlayerClient {
    pub const RETRY_ORDER: [PlayerClient; 5] = [
        PlayerClient::Default,
        PlayerClient::Android,
        PlayerClient::TvEmbedded,
        PlayerClient::Ios,
        PlayerClient::Web,
    ];

    pub fn as_str(self) -> &'static str {
        match self {
            Self::Default => "default",
            Self::Android => "android",
            // NOTE: upstream removed the `tv_embedded` client as broken, so the
            // embedded slot maps to the supported `web_embedded` client instead.
            Self::TvEmbedded => "web_embedded",
            Self::Ios => "ios",
            Self::Web => "web",
        }
    }
}

pub fn player_client_options<I>(clients: I) -> Vec<String>
where
    I: IntoIterator<Item = PlayerClient>,
{
    clients
        .into_iter()
        .map(|client| {
            if client == PlayerClient::Default {
                return "{}".to_owned();
            }

            serde_json::json!({
                "extractor_args": {
                    "youtube": {
                        "player_client": [client.as_str()],
                    },
                },
            })
            .to_string()
        })
        .collect()
}

pub struct Client {
    raw: *mut sys::LyContext,
    options: String,
}

pub struct ClientBuilder {
    options: serde_json::Map<String, serde_json::Value>,
}

impl ClientBuilder {
    pub fn new() -> Self {
        Self {
            options: serde_json::Map::new(),
        }
    }

    pub fn options_json(mut self, options: &str) -> Result<Self, Error> {
        let parsed: serde_json::Value = serde_json::from_str(options)
            .map_err(|failed| Error::new(Kind::InvalidArgument, failed.to_string()))?;

        let object = parsed
            .as_object()
            .ok_or_else(|| Error::new(Kind::InvalidArgument, "options must be a JSON object"))?;

        for (key, value) in object {
            self.options.insert(key.clone(), value.clone());
        }

        Ok(self)
    }

    pub fn option(mut self, key: &str, value: serde_json::Value) -> Self {
        self.options.insert(key.to_owned(), value);

        self
    }

    pub fn extractor_args(mut self, plugin: &str, args: serde_json::Value) -> Self {
        let slot = self
            .options
            .entry("extractor_args")
            .or_insert_with(|| serde_json::Value::Object(serde_json::Map::new()));

        if !slot.is_object() {
            *slot = serde_json::Value::Object(serde_json::Map::new());
        }

        if let Some(table) = slot.as_object_mut() {
            table.insert(plugin.to_owned(), args);
        }

        self
    }

    pub fn youtube_player_client(self, client: PlayerClient) -> Self {
        if client == PlayerClient::Default {
            return self;
        }

        self.extractor_args(
            "youtube",
            serde_json::json!({"player_client": [client.as_str()]}),
        )
    }

    pub fn build(self) -> Result<Client, Error> {
        let text = serde_json::to_string(&serde_json::Value::Object(self.options))
            .map_err(|failed| Error::new(Kind::Internal, failed.to_string()))?;

        Client::new(&text)
    }
}

impl Default for ClientBuilder {
    fn default() -> Self {
        Self::new()
    }
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

        Ok(Self {
            raw: context,
            options: options_json.to_owned(),
        })
    }

    pub fn builder() -> ClientBuilder {
        ClientBuilder::new()
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
                ptr::null(),
                0,
                &mut result,
                &mut error,
            )
        };

        if status != sys::LY_OK || result.is_null() {
            return Err(Error::from_ptr(error));
        }

        MediaInfo::from_raw(result)
    }

    #[tracing::instrument(name = "libytdlp.extract", level = "debug", skip(self, url, options))]
    pub fn extract_with_options(&self, url: &str, options: &str) -> Result<MediaInfo, Error> {
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

        let (options_ptr, options_len) = if options.is_empty() {
            (ptr::null(), 0)
        } else {
            (options.as_ptr().cast(), options.len())
        };

        let mut result: *mut sys::LyResult = ptr::null_mut();
        let mut error: *mut sys::LyError = ptr::null_mut();

        let status = unsafe {
            sys::ly_extract(
                self.raw,
                url.as_ptr().cast(),
                url.len(),
                options_ptr,
                options_len,
                &mut result,
                &mut error,
            )
        };

        if status != sys::LY_OK || result.is_null() {
            return Err(Error::from_ptr(error));
        }

        MediaInfo::from_raw(result)
    }

    #[tracing::instrument(name = "libytdlp.extract_async", level = "debug", skip(self, url))]
    pub fn extract_async(&self, url: &str) -> WaitForExtract {
        WaitForExtract {
            pending: Some((url.to_owned(), self.options.clone())),
            shared: Arc::new(Shared {
                waker: Mutex::new(None),
                outcome: Mutex::new(None),
            }),
        }
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

struct Shared {
    waker: Mutex<Option<Waker>>,
    outcome: Mutex<Option<Result<MediaInfo, Error>>>,
}

pub struct WaitForExtract {
    pending: Option<(String, String)>,
    shared: Arc<Shared>,
}

impl Future for WaitForExtract {
    type Output = Result<MediaInfo, Error>;

    fn poll(mut self: Pin<&mut Self>, context: &mut Context<'_>) -> Poll<Self::Output> {
        if let Some(outcome) = self.shared.outcome.lock().expect("outcome lock").take() {
            return Poll::Ready(outcome);
        }

        *self.shared.waker.lock().expect("waker lock") = Some(context.waker().clone());

        if let Some(outcome) = self.shared.outcome.lock().expect("outcome lock").take() {
            return Poll::Ready(outcome);
        }

        let Some((url, options)) = self.pending.take() else {
            return Poll::Pending;
        };

        let shared = Arc::clone(&self.shared);

        std::thread::spawn(move || {
            let outcome = isolated_extract(url, options);

            *shared.outcome.lock().expect("outcome lock") = Some(outcome);

            let waker = shared.waker.lock().expect("waker lock").take();

            if let Some(waker) = waker {
                waker.wake();
            }
        });

        Poll::Pending
    }
}

fn isolated_extract(url: String, options: String) -> Result<MediaInfo, Error> {
    let mut context: *mut sys::LyContext = ptr::null_mut();
    let mut error: *mut sys::LyError = ptr::null_mut();

    let status = unsafe {
        sys::ly_context_create(
            options.as_ptr().cast(),
            options.len(),
            &mut context,
            &mut error,
        )
    };

    if status != sys::LY_OK || context.is_null() {
        return Err(Error::from_ptr(error));
    }

    let client = Client {
        raw: context,
        options,
    };

    client.extract(&url)
}
