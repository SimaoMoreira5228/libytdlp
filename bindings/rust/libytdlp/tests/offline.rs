use libytdlp::Client;
use libytdlp::Kind;
use libytdlp::MediaInfo;
use libytdlp::abi_version;
use libytdlp::runtime_info;

fn assert_send_sync<T>()
where
    T: Send + Sync,
{
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
}

#[test]
fn empty_url_fails() {
    let client = Client::new("{}").expect("default options must be accepted");

    for url in ["", "   ".trim()] {
        let error = client.extract(url).expect_err("empty url must fail");

        assert_eq!(error.kind(), Kind::InvalidArgument);
        assert!(!error.message().is_empty());
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
    assert_eq!(info.backend(), "embedded");
    assert!(!info.profile().is_empty());
    assert!(!info.python().is_empty());
    assert!(!info.ytdlp_version().is_empty());
    assert!(!info.ytdlp_commit().is_empty());
    assert!(!info.raw_json().is_empty());
    assert!(info.value().is_object());
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
