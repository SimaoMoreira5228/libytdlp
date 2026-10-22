use std::io::BufRead;
use std::io::BufReader;
use std::path::PathBuf;
use std::process::Child;
use std::process::Command;
use std::process::Stdio;
use std::time::Duration;
use std::time::Instant;

use libytdlp::JobState;
use libytdlp::Kind;
use libytdlp::ProcessClient;
use libytdlp::ProcessConfig;

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

fn test_client(timeout: Option<Duration>) -> ProcessClient {
    let mut config = ProcessConfig::new();

    if let Ok(executable) = std::env::var("LIBYTDLP_YTDLP") {
        config = config.executable(executable);
    }

    if let Some(window) = timeout {
        config = config.timeout(window);
    }

    ProcessClient::new(config).expect("yt-dlp executable must resolve")
}

fn temp_output(name: &str) -> (PathBuf, PathBuf, String) {
    let dir = std::env::temp_dir().join(format!("libytdlp-proctest-{}-{name}", std::process::id()));

    let _ = std::fs::remove_dir_all(&dir);
    std::fs::create_dir_all(&dir).expect("temp dir must be created");

    let target = dir.join("out.bin");

    let info = PathBuf::from(format!("{}.info.json", target.display()));

    let options = format!(
        "{{\"outtmpl\": \"{}\", \"proxy\": \"\"}}",
        target.to_string_lossy().replace('\\', "\\\\")
    );

    (target, info, options)
}

#[test]
fn explicit_override_miss_is_invalid_argument() {
    let config = ProcessConfig::new().executable("/nonexistent-dir-xyz/yt-dlp-missing-abc");

    let failed = ProcessClient::new(config).expect_err("missing override must fail");

    assert_eq!(failed.kind(), Kind::InvalidArgument);
}

#[test]
fn stalled_download_kill_is_prompt_and_clean() {
    let server = FixtureServer::start(32_000_000, 150_000);
    let (target, info, options) = temp_output("kill");

    let client = test_client(None);
    let job = client
        .download(&server.url("/big.bin"), &options)
        .expect("job must start");

    let boot = Instant::now();

    loop {
        let (state, events) = job.poll().expect("poll must succeed");

        assert_eq!(state, JobState::Running);

        if !events.is_empty() {
            assert!(
                events
                    .iter()
                    .any(|event| event.downloaded_bytes().is_some())
            );

            break;
        }

        assert!(
            boot.elapsed() < Duration::from_secs(25),
            "stalled download never produced progress"
        );

        std::thread::sleep(Duration::from_millis(10));
    }

    let pid = job.child_id().expect("running job must have a pid");

    let kill_start = Instant::now();

    job.cancel();

    let error = job.wait().expect_err("killed job must fail");

    let latency = kill_start.elapsed();

    eprintln!("process kill latency: {latency:?}");

    assert_eq!(error.kind(), Kind::Cancelled);
    assert!(
        latency < Duration::from_secs(10),
        "kill took too long: {latency:?}"
    );
    assert_eq!(job.child_id(), None, "child must be reaped");

    assert!(!target.exists(), "killed download must leave no target");
    assert!(!info.exists(), "killed download must leave no info JSON");

    #[cfg(target_os = "linux")]
    assert!(
        !PathBuf::from(format!("/proc/{pid}")).exists(),
        "child {pid} must be reaped"
    );

    let (state, events) = job.poll().expect("poll on killed job must succeed");

    assert_eq!(state, JobState::Cancelled);
    assert!(events.is_empty());

    job.cancel();

    let error = job.wait().expect_err("wait must be idempotent");

    assert_eq!(error.kind(), Kind::Cancelled);

    let parent = target.parent().expect("output must have a parent");
    let _ = std::fs::remove_dir_all(parent);
}

#[test]
fn stalled_download_timeout_cancels() {
    let server = FixtureServer::start(32_000_000, 150_000);
    let (target, info, options) = temp_output("timeout");

    let client = test_client(Some(Duration::from_secs(3)));
    let job = client
        .download(&server.url("/big.bin"), &options)
        .expect("job must start");

    let start = Instant::now();

    let error = job.wait().expect_err("timed download must fail");

    let elapsed = start.elapsed();

    eprintln!("process timeout latency: {elapsed:?}");

    assert_eq!(error.kind(), Kind::Cancelled);
    assert!(
        elapsed < Duration::from_secs(20),
        "timeout took too long: {elapsed:?}"
    );
    assert!(!target.exists(), "timed out download must leave no target");
    assert!(!info.exists(), "timed out download must leave no info JSON");

    let parent = target.parent().expect("output must have a parent");
    let _ = std::fs::remove_dir_all(parent);
}

#[test]
fn bogus_url_fails_as_extract() {
    let (_target, _info, options) = temp_output("bogus");

    let client = test_client(None);
    let job = client
        .download("ly-bogus://no-such-scheme-xyz", &options)
        .expect("job must start");

    let error = job.wait().expect_err("bogus url must fail");

    assert_eq!(error.kind(), Kind::Extract);
    assert!(!error.message().is_empty());
    assert!(!error.is_retryable());

    let (state, events) = job.poll().expect("poll on failed job must succeed");

    assert_eq!(state, JobState::Failed);
    assert!(events.is_empty());
}

#[test]
fn closed_port_fails_as_retryable_network() {
    let listener = std::net::TcpListener::bind("127.0.0.1:0").expect("loopback bind must succeed");

    let port = listener
        .local_addr()
        .expect("bound socket must have an address")
        .port();

    drop(listener);

    let (_target, _info, options) = temp_output("closed");

    let client = test_client(None);
    let job = client
        .download(&format!("http://127.0.0.1:{port}/file.bin"), &options)
        .expect("job must start");

    let error = job.wait().expect_err("closed port must fail");

    assert_eq!(error.kind(), Kind::Network);
    assert!(error.is_retryable());
}

#[test]
fn fast_download_completes_with_info() {
    let server = FixtureServer::start(65536, 8_000_000);
    let (target, _info, options) = temp_output("fast");

    let client = test_client(None);
    let job = client
        .download(&server.url("/file.bin"), &options)
        .expect("job must start");

    let info = job.wait().expect("fast download must succeed");

    assert!(info.raw_json().len() > 100);
    assert!(info.value().is_object());

    let (state, events) = job.poll().expect("poll on done job must succeed");

    assert_eq!(state, JobState::Done);
    assert!(events.is_empty());

    let bytes = std::fs::read(&target).expect("output file must exist");

    assert_eq!(bytes.len(), 65536);

    let parent = target.parent().expect("output must have a parent");
    let _ = std::fs::remove_dir_all(parent);
}
