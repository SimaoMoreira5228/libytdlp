use std::io::BufRead;
use std::io::BufReader;
use std::path::PathBuf;
use std::process::Child;
use std::process::Command;
use std::process::Stdio;

use libytdlp::Client;
use libytdlp::ClientBuilder;
use libytdlp::Kind;
use libytdlp::MediaInfo;
use libytdlp::PlayerClient;
use libytdlp::abi_version;
use libytdlp::player_client_options;
use libytdlp::runtime_info;

fn assert_send_sync<T>()
where
    T: Send + Sync,
{
}

struct FixtureServer {
    child: Child,
    port: u16,
}

impl FixtureServer {
    fn start(bytes: u64, rate: u64) -> Self {
        let fixture = PathBuf::from(env!("CARGO_MANIFEST_DIR"))
            .join("../../../tests/fixtures/slow_server.py");

        let python = std::env::var("LIBYTDLP_TEST_PYTHON").unwrap_or_else(|_| "python3".to_owned());

        let mut child = Command::new(python)
            .arg(fixture)
            .arg("0")
            .arg(bytes.to_string())
            .arg(rate.to_string())
            .stdout(Stdio::piped())
            .stderr(Stdio::null())
            .spawn()
            .expect("fixture server must start");

        let stdout = child
            .stdout
            .take()
            .expect("fixture server must pipe its port");
        let mut line = String::new();
        BufReader::new(stdout)
            .read_line(&mut line)
            .expect("fixture server must report its port");

        let port: u16 = line
            .trim()
            .strip_prefix("PORT ")
            .expect("fixture server must print PORT")
            .parse()
            .expect("fixture port must be numeric");

        Self { child, port }
    }

    fn url(&self, path: &str) -> String {
        format!("http://127.0.0.1:{}{}", self.port, path)
    }
}

impl Drop for FixtureServer {
    fn drop(&mut self) {
        let _ = self.child.kill();
        let _ = self.child.wait();
    }
}

#[test]
fn handles_are_send_and_sync() {
    assert_send_sync::<Client>();
    assert_send_sync::<MediaInfo>();
}

#[test]
fn bad_options_json_fails_as_invalid_argument() {
    let client = Client::new("{not json").expect("options are stored verbatim");

    let error = client
        .extract("ly-bogus://example")
        .expect_err("bad options must fail");

    assert_eq!(error.kind(), Kind::InvalidArgument);
    assert!(!error.message().is_empty());
    assert!(!error.is_retryable());
}

#[test]
fn empty_url_fails() {
    let client = Client::new("{}").expect("default options must be accepted");

    for url in ["", "   ".trim()] {
        let error = client.extract(url).expect_err("empty url must fail");

        assert_eq!(error.kind(), Kind::InvalidArgument);
        assert!(!error.message().is_empty());
        assert!(!error.is_retryable());
    }
}

#[test]
fn nul_byte_url_fails_without_crashing() {
    let client = Client::new("{}").expect("default options must be accepted");

    let error = client
        .extract("https://example.org/watch?v=abc\0def")
        .expect_err("NUL byte url must fail");

    assert_eq!(error.kind(), Kind::InvalidArgument);
    assert!(!error.message().is_empty());
}

#[test]
fn create_drop_loop() {
    for _ in 0..200 {
        let client = Client::new("{}").expect("context creation must succeed");

        std::hint::black_box(&client);
    }
}

#[test]
fn runtime_info_reports_diagnostics() {
    let info = runtime_info().expect("runtime info must succeed");

    assert_eq!(info.abi(), abi_version());
    assert_eq!(info.abi(), libytdlp_sys::LY_ABI_VERSION);
    assert_eq!(info.backend(), "embedded");
    assert!(!info.profile().is_empty());
    assert!(!info.python().is_empty());
    assert!(!info.ytdlp_version().is_empty());
    assert!(!info.ytdlp_commit().is_empty());
    assert!(!info.raw_json().is_empty());
    assert!(info.value().is_object());

    let ffmpeg_some = info.ffmpeg().is_some();
    let ffprobe_some = info.ffprobe().is_some();

    assert_eq!(
        ffmpeg_some,
        info.value()
            .get("ffmpeg")
            .and_then(serde_json::Value::as_str)
            .is_some()
    );
    assert_eq!(
        ffprobe_some,
        info.value()
            .get("ffprobe")
            .and_then(serde_json::Value::as_str)
            .is_some()
    );

    if let Some(path) = info.ffmpeg() {
        assert!(!path.is_empty());
    }

    if let Some(path) = info.ffprobe() {
        assert!(!path.is_empty());
    }
}

#[test]
fn error_display_reports_kind_and_message() {
    let client = Client::new("{not json").expect("options are stored verbatim");

    let error = client
        .extract("ly-bogus://example")
        .expect_err("bad options must fail");

    let shown = error.to_string();

    assert!(shown.contains("invalid argument"), "display was: {shown}");
    assert!(shown.contains(error.message()), "display was: {shown}");

    let debug = format!("{error:?}");

    assert!(debug.contains("InvalidArgument"), "debug was: {debug}");
}

#[test]
fn builder_assembles_typed_options() {
    let client = ClientBuilder::new()
        .options_json(r#"{"quiet": true, "retries": 1}"#)
        .expect("object options must merge")
        .options_json(r#"{"retries": 3}"#)
        .expect("later keys must win")
        .option("socket_timeout", serde_json::json!(5))
        .extractor_args("generic", serde_json::json!({"force": [true]}))
        .extractor_args("other", serde_json::json!({"x": [1]}))
        .build()
        .expect("builder must produce a client");

    let error = client
        .extract("ly-bogus://no-such-scheme-xyz")
        .expect_err("bogus url must fail");

    assert!(!error.message().is_empty());
}

#[test]
fn builder_rejects_non_object_options() {
    let failed = ClientBuilder::new().options_json("[1, 2]");

    assert!(failed.is_err());

    let failed = ClientBuilder::new().options_json("{not json}");

    assert!(failed.is_err());
}

#[test]
fn builder_default_matches_empty_options() {
    let _ = ClientBuilder::default()
        .build()
        .expect("default builder must succeed");
}

#[test]
fn player_client_retry_order_starts_with_default() {
    assert_eq!(PlayerClient::RETRY_ORDER.len(), 5);
    assert_eq!(PlayerClient::RETRY_ORDER[0], PlayerClient::Default);
    assert_eq!(
        PlayerClient::RETRY_ORDER,
        [
            PlayerClient::Default,
            PlayerClient::Android,
            PlayerClient::TvEmbedded,
            PlayerClient::Ios,
            PlayerClient::Web,
        ]
    );
}

#[test]
fn player_client_wire_names_match_upstream() {
    assert_eq!(PlayerClient::Default.as_str(), "default");
    assert_eq!(PlayerClient::Android.as_str(), "android");
    assert_eq!(PlayerClient::TvEmbedded.as_str(), "web_embedded");
    assert_eq!(PlayerClient::Ios.as_str(), "ios");
    assert_eq!(PlayerClient::Web.as_str(), "web");
}

#[test]
fn player_client_options_serialize_per_entry() {
    assert_eq!(player_client_options([PlayerClient::Default]), ["{}"]);

    let options = player_client_options([PlayerClient::Ios]);

    assert_eq!(options.len(), 1);
    assert!(
        options[0].contains("\"ios\""),
        "options were: {}",
        options[0]
    );

    let parsed: serde_json::Value =
        serde_json::from_str(&options[0]).expect("options must be JSON");

    assert_eq!(
        parsed,
        serde_json::json!({
            "extractor_args": {
                "youtube": {
                    "player_client": ["ios"],
                },
            },
        })
    );

    let ordered = player_client_options(PlayerClient::RETRY_ORDER);

    assert_eq!(ordered.len(), 5);
    assert_eq!(ordered[0], "{}");
    assert!(
        ordered[3].contains("\"ios\""),
        "options were: {}",
        ordered[3]
    );
}

#[test]
fn youtube_player_client_reaches_youtube_dl() {
    let server = FixtureServer::start(65536, 8_000_000);
    let url = server.url("/file.bin");

    let client = Client::builder()
        // NOTE: force direct connections so the loopback fixture ignores any
        // ambient proxy env; env scrubbing would race across parallel tests.
        .options_json("{\"proxy\": \"\"}")
        .expect("direct proxy options must merge")
        .youtube_player_client(PlayerClient::Ios)
        .build()
        .expect("builder must succeed");

    let info = client
        .extract(&url)
        .expect("ios override in context must keep generic extract working");

    assert_eq!(info.id(), Some("file"));

    let plain = Client::new("{\"proxy\": \"\"}").expect("default options must be accepted");
    let per_call = player_client_options([PlayerClient::Ios]);

    assert_eq!(per_call.len(), 1);

    let info = plain
        .extract_with_options(&url, &per_call[0])
        .expect("ios override per call must keep generic extract working");

    assert_eq!(info.id(), Some("file"));
}

#[test]
fn extract_first_retries_then_succeeds() {
    let server = FixtureServer::start(65536, 8_000_000);
    let url = server.url("/file.bin");

    let client = Client::new("{}").expect("default options must be accepted");

    let info = client
        // NOTE: proxy "" forces a direct connection so the loopback fixture
        // ignores any ambient proxy env.
        .extract_first(
            &url,
            vec!["{not json}".to_owned(), "{\"proxy\": \"\"}".to_owned()],
        )
        .expect("second options must succeed");

    assert_eq!(info.id(), Some("file"));
}

#[test]
fn extract_first_reports_last_error() {
    let server = FixtureServer::start(65536, 8_000_000);
    let url = server.url("/file.bin");

    let client = Client::new("{}").expect("default options must be accepted");

    let error = client
        .extract_first(&url, Vec::new())
        .expect_err("empty options must fail");

    assert_eq!(error.kind(), Kind::InvalidArgument);

    let error = client
        .extract_first(&url, vec!["{not json}".to_owned()])
        .expect_err("all-bad options must fail");

    assert!(!error.message().is_empty());
}

#[test]
fn extract_with_options_rejects_nul_bytes() {
    let client = Client::new("{}").expect("default options must be accepted");

    let error = client
        .extract_with_options("https://example.org/x\0y", "{}")
        .expect_err("NUL url must fail");

    assert_eq!(error.kind(), Kind::InvalidArgument);

    let error = client
        .extract_with_options("ly-bogus://example", "{}\0")
        .expect_err("NUL options must fail");

    assert_eq!(error.kind(), Kind::InvalidArgument);
}

#[test]
fn extract_with_options_rejects_non_object_call_options() {
    let client = Client::new("{}").expect("default options must be accepted");

    let error = client
        .extract_with_options("ly-bogus://example", "[1, 2]")
        .expect_err("non-object call options must fail");

    assert_eq!(error.kind(), Kind::InvalidArgument);
}

#[test]
fn extract_with_options_merges_over_context() {
    let server = FixtureServer::start(65536, 8_000_000);
    let url = server.url("/file.bin");

    let client = ClientBuilder::new()
        // NOTE: proxy "" forces a direct connection so the loopback fixture
        // ignores any ambient proxy env.
        .options_json(r#"{"skip_download": true, "proxy": ""}"#)
        .expect("context options must merge")
        .build()
        .expect("builder must succeed");

    let info = client
        .extract_with_options(&url, "{}")
        .expect("empty call options must keep context");

    assert_eq!(info.id(), Some("file"));

    let info = client
        .extract_with_options(
            &url,
            r#"{"extractor_args": {"generic": {"force": [true]}}}"#,
        )
        .expect("call extractor args must reach YoutubeDL");

    assert_eq!(info.id(), Some("file"));

    let plain = Client::new("{\"proxy\": \"\"}").expect("default options must be accepted");
    let info = plain.extract(&url).expect("context extract must work");

    assert_eq!(info.id(), Some("file"));
    assert!(info.entries().is_empty());
    assert!(info.formats().iter().all(|format| format.url().is_some()));
}
