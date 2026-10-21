use std::io::BufRead;
use std::io::BufReader;
use std::path::PathBuf;
use std::process::Child;
use std::process::Command;
use std::process::Stdio;
use std::time::Duration;
use std::time::Instant;

use libytdlp::Client;
use libytdlp::DownloadJob;
use libytdlp::JobState;
use libytdlp::Kind;

fn assert_send_sync<T>()
where
    T: Send + Sync,
{
}

#[test]
fn job_handle_is_send_and_sync() {
    assert_send_sync::<DownloadJob>();
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

fn wait_for_state(job: &DownloadJob, want: JobState, timeout: Duration) -> JobState {
    let start = Instant::now();
    loop {
        let (state, _) = job.poll().expect("poll must succeed");
        if state == want || state == JobState::Failed || start.elapsed() > timeout {
            return state;
        }
        std::thread::sleep(Duration::from_millis(10));
    }
}

fn temp_output(name: &str) -> (PathBuf, String) {
    let dir =
        std::env::temp_dir().join(format!("libytdlp-jobtest-{}-{}", std::process::id(), name));

    let _ = std::fs::remove_dir_all(&dir);
    std::fs::create_dir_all(&dir).expect("temp dir must be created");

    let target = dir.join("out.bin");

    let options = format!(
        "{{\"outtmpl\": \"{}\"}}",
        target.to_string_lossy().replace('\\', "\\\\")
    );

    (target, options)
}

#[test]
fn bogus_url_job_fails_as_extract() {
    let client = Client::new("{}").expect("default options must be accepted");

    let job = client
        .download("ly-bogus://no-such-scheme-xyz", "{}")
        .expect("job must start");

    let error = job.wait().expect_err("bogus url must fail");

    assert_eq!(error.kind(), Kind::Extract);
    assert!(!error.message().is_empty());
    assert!(error.exception_class().is_some());
    assert!(!error.is_retryable());

    let error = job.wait().expect_err("wait must be idempotent");

    assert_eq!(error.kind(), Kind::Extract);

    let (state, events) = job.poll().expect("poll on failed job must succeed");

    assert_eq!(state, JobState::Failed);
    assert!(events.is_empty());

    job.cancel();
}

#[test]
fn slow_download_cancels() {
    let server = FixtureServer::start(20_000_000, 200_000);
    let (target, options) = temp_output("cancel");

    let client = Client::new(&options).expect("options must be accepted");
    let job = client
        .download(&server.url("/big.bin"), "{}")
        .expect("job must start");

    let state = wait_for_state(&job, JobState::Running, Duration::from_secs(10));

    assert_eq!(state, JobState::Running);

    let (running, _) = job.poll().expect("poll must succeed");

    assert_eq!(running, JobState::Running);

    job.cancel();
    job.cancel();

    let error = job.wait().expect_err("cancelled job must fail");

    assert_eq!(error.kind(), Kind::Cancelled);
    assert!(!error.is_retryable());

    let error = job.wait().expect_err("wait must be idempotent");

    assert_eq!(error.kind(), Kind::Cancelled);

    let (state, events) = job.poll().expect("poll on cancelled job must succeed");

    assert_eq!(state, JobState::Cancelled);
    assert!(events.is_empty());

    job.cancel();

    let parent = target.parent().expect("output must have a parent");
    let _ = std::fs::remove_dir_all(parent);
}

#[test]
fn fast_download_completes() {
    let server = FixtureServer::start(65536, 8_000_000);
    let (target, options) = temp_output("success");

    let client = Client::new(&options).expect("options must be accepted");
    let job = client
        .download(&server.url("/file.bin"), "{}")
        .expect("job must start");

    let info = job.wait().expect("fast download must succeed");

    assert!(info.raw_json().len() > 100);
    assert!(info.value().is_object());

    let (state, events) = job.poll().expect("poll on done job must succeed");

    assert_eq!(state, JobState::Done);
    assert!(events.is_empty());

    let info = job.wait().expect("wait must be idempotent");

    assert!(info.raw_json().len() > 100);

    let bytes = std::fs::read(&target).expect("output file must exist");

    assert_eq!(bytes.len(), 65536);

    let parent = target.parent().expect("output must have a parent");
    let _ = std::fs::remove_dir_all(parent);
}

#[test]
fn per_download_options_override_context() {
    let server = FixtureServer::start(65536, 8_000_000);

    let stem = format!("override-{}", std::process::id());
    let dir_a = std::env::temp_dir().join(format!("libytdlp-jobtest-{stem}-ctx"));
    let dir_b = std::env::temp_dir().join(format!("libytdlp-jobtest-{stem}-job"));

    let _ = std::fs::remove_dir_all(&dir_a);
    let _ = std::fs::remove_dir_all(&dir_b);
    std::fs::create_dir_all(&dir_a).expect("context dir must be created");
    std::fs::create_dir_all(&dir_b).expect("job dir must be created");

    let context_options = format!(
        "{{\"outtmpl\": \"{}\"}}",
        dir_a
            .join("ctx.bin")
            .to_string_lossy()
            .replace('\\', "\\\\")
    );
    let job_options = format!(
        "{{\"outtmpl\": \"{}\"}}",
        dir_b
            .join("job.bin")
            .to_string_lossy()
            .replace('\\', "\\\\")
    );

    let client = Client::new(&context_options).expect("options must be accepted");
    let job = client
        .download(&server.url("/file.bin"), &job_options)
        .expect("job must start");

    let info = job.wait().expect("override download must succeed");

    assert!(info.raw_json().len() > 100);

    let bytes = std::fs::read(dir_b.join("job.bin")).expect("job output must exist");

    assert_eq!(bytes.len(), 65536);

    let mut leftovers = std::fs::read_dir(&dir_a).expect("context dir must be readable");

    assert!(
        leftovers.next().is_none(),
        "context outtmpl must not be used"
    );

    let _ = std::fs::remove_dir_all(&dir_a);
    let _ = std::fs::remove_dir_all(&dir_b);
}

#[test]
fn release_joins_running_job() {
    let server = FixtureServer::start(65536, 8_000_000);
    let (target, options) = temp_output("release");

    let client = Client::new(&options).expect("options must be accepted");
    let job = client
        .download(&server.url("/file.bin"), "{}")
        .expect("job must start");

    drop(job);

    let parent = target.parent().expect("output must have a parent");
    let _ = std::fs::remove_dir_all(parent);

    let _ = server;
}

fn closed_port() -> u16 {
    let listener = std::net::TcpListener::bind("127.0.0.1:0").expect("loopback bind must succeed");

    listener
        .local_addr()
        .expect("bound socket must have an address")
        .port()
}

#[test]
fn closed_port_download_fails_as_non_retryable_network_error() {
    let scrubbed = [
        "http_proxy",
        "https_proxy",
        "all_proxy",
        "HTTP_PROXY",
        "HTTPS_PROXY",
        "ALL_PROXY",
    ];

    let saved: Vec<(String, Option<String>)> = scrubbed
        .iter()
        .map(|name| {
            let previous = std::env::var(name).ok();
            unsafe { std::env::remove_var(name) };
            (name.to_string(), previous)
        })
        .collect();

    unsafe { std::env::set_var("no_proxy", "*") };
    unsafe { std::env::set_var("NO_PROXY", "*") };

    let port = closed_port();

    let (target, options) = temp_output("closed");
    let client = Client::new(&options).expect("options must be accepted");

    let job = client
        .download(&format!("http://127.0.0.1:{port}/file.bin"), "{}")
        .expect("job must start");

    let error = job.wait().expect_err("closed port must fail");

    assert_eq!(error.kind(), Kind::Network);
    assert!(error.is_retryable());

    let parent = target.parent().expect("output must have a parent");
    let _ = std::fs::remove_dir_all(parent);

    for (name, previous) in saved {
        if let Some(value) = previous {
            unsafe { std::env::set_var(name, value) };
        } else {
            unsafe { std::env::remove_var(name) };
        }
    }

    unsafe { std::env::remove_var("no_proxy") };
    unsafe { std::env::remove_var("NO_PROXY") };
}
