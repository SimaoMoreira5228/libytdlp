//! EXPERIMENTAL process backend. Downloads run through a pinned yt-dlp child process.
//!
//! This is a prototype, not a second supported API. It answers one question: does a killable
//! subprocess backend earn a place behind the C ABI? Expect the surface to move.
//!
//! A `ProcessClient` spawns one `yt-dlp` per download, argv only, never a shell, with a `--`
//! separator keeping the URL from parsing as a flag. Progress arrives over `--progress-template`,
//! metadata over `--write-info-json`, failures over exit status plus stderr. `poll`, `wait`, and
//! `cancel` mirror `DownloadJob`, except cancel kills the whole process tree: a stalled transfer
//! dies fast instead of hanging your process. There is no async wrapper yet.
//!
//! Only `outtmpl` and `proxy` exist, and `outtmpl` must be literal: template fields fail loudly so
//! cleanup and info lookup stay exact. Failures map to native `Kind` values (`Extract` for bad
//! URLs, `Network` for transport, `Runtime` for a missing binary, `Cancelled` for kills and
//! timeouts). Cancel wins every race, and the timeout covers the whole `wait`, not per-byte stalls.

use std::io::BufRead;
use std::io::Read;
use std::path::Path;
use std::path::PathBuf;
use std::process::Child;
use std::process::Command;
use std::process::Stdio;
use std::sync::Arc;
use std::sync::Mutex;
use std::time::Duration;
use std::time::Instant;

use crate::error::Error;
use crate::error::Kind;
use crate::job::JobState;
use crate::media::MediaInfo;
use crate::progress::ProgressEvent;

const PROGRESS_PREFIX: &str = "__LIBYTDLP__ ";

const PROGRESS_TEMPLATE: &str = "__LIBYTDLP__ {\"status\": \"downloading\", \"downloaded_bytes\": \"%(progress.downloaded_bytes)s\", \"total_bytes\": \"%(progress.total_bytes)s\", \"total_bytes_estimate\": \"%(progress.total_bytes_estimate)s\", \"filename\": \"%(info._filename)s\"}";

const STDERR_CAP: usize = 262_144;

const TRANSPORT_MARKERS: [&str; 7] = [
    "Unable to download webpage",
    "Failed to establish a new connection",
    "Connection refused",
    "Network is unreachable",
    "Temporary failure in name resolution",
    "Connection reset by peer",
    "timed out",
];

const PROXY_ENV: [&str; 6] = [
    "http_proxy",
    "https_proxy",
    "all_proxy",
    "HTTP_PROXY",
    "HTTPS_PROXY",
    "ALL_PROXY",
];

#[derive(Debug, Clone, Default)]
pub struct ProcessConfig {
    executable: Option<PathBuf>,
    timeout: Option<Duration>,
}

impl ProcessConfig {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn executable(mut self, path: impl Into<PathBuf>) -> Self {
        self.executable = Some(path.into());

        self
    }

    pub fn timeout(mut self, window: Duration) -> Self {
        self.timeout = Some(window);

        self
    }
}

#[derive(Debug)]
pub struct ProcessClient {
    executable: PathBuf,
    timeout: Option<Duration>,
}

impl ProcessClient {
    pub fn new(config: ProcessConfig) -> Result<Self, Error> {
        let executable = resolve_executable(&config)?;

        Ok(Self {
            executable,
            timeout: config.timeout,
        })
    }

    #[tracing::instrument(name = "libytdlp.process.download", level = "debug", skip(self, url))]
    pub fn download(&self, url: &str, options: &str) -> Result<ProcessDownloadJob, Error> {
        check_text("url", url)?;

        if url.is_empty() {
            return Err(Error::new(Kind::InvalidArgument, "url must not be empty"));
        }

        check_text("options", options)?;

        let parsed = parse_options(options)?;

        let args = argv(&parsed, url);

        let mut child = spawn(&self.executable, &args, parsed.proxy.as_deref() == Some(""))?;

        let stdout = child.stdout.take().expect("stdout was piped");

        let stderr = child.stderr.take().expect("stderr was piped");

        let events = Arc::new(Mutex::new(Vec::new()));
        let stderr_bytes = Arc::new(Mutex::new(Vec::new()));
        let lines_seen = Arc::new(Mutex::new(0u64));

        let (stdout_pump, stderr_pump) = spawn_pumps(
            stdout,
            stderr,
            Arc::clone(&events),
            Arc::clone(&stderr_bytes),
            Arc::clone(&lines_seen),
        );

        let deadline = self.timeout.map(|window| Instant::now() + window);

        let inner = JobInner {
            child: Some(child),
            stdout_pump: Some(stdout_pump),
            stderr_pump: Some(stderr_pump),
            stderr_bytes,
            killed: false,
            timed_out: false,
            deadline,
            target: PathBuf::from(&parsed.outtmpl),
            outcome: None,
        };

        Ok(ProcessDownloadJob {
            inner: Mutex::new(inner),
            events,
            lines_seen,
        })
    }
}

enum Outcome {
    Done(String),
    Failed(Error),
}

struct JobInner {
    child: Option<Child>,
    stdout_pump: Option<std::thread::JoinHandle<()>>,
    stderr_pump: Option<std::thread::JoinHandle<()>>,
    stderr_bytes: Arc<Mutex<Vec<u8>>>,
    killed: bool,
    timed_out: bool,
    deadline: Option<Instant>,
    target: PathBuf,
    outcome: Option<Outcome>,
}

pub struct ProcessDownloadJob {
    inner: Mutex<JobInner>,
    events: Arc<Mutex<Vec<ProgressEvent>>>,
    lines_seen: Arc<Mutex<u64>>,
}

impl ProcessDownloadJob {
    #[tracing::instrument(name = "libytdlp.process.poll", level = "debug", skip(self))]
    pub fn poll(&self) -> Result<(JobState, Vec<ProgressEvent>), Error> {
        let inner = self.settled();

        let events = std::mem::take(&mut *self.events.lock().expect("events lock"));

        match &inner.outcome {
            None => Ok((JobState::Running, events)),
            Some(Outcome::Done(_)) => Ok((JobState::Done, Vec::new())),
            Some(Outcome::Failed(error)) => Ok((state_of(error), Vec::new())),
        }
    }

    #[tracing::instrument(name = "libytdlp.process.wait", level = "debug", skip(self))]
    pub fn wait(&self) -> Result<MediaInfo, Error> {
        loop {
            let done = self
                .settled()
                .outcome
                .as_ref()
                .map(|outcome| match outcome {
                    Outcome::Done(raw) => MediaInfo::from_json_text(raw.clone()),
                    Outcome::Failed(error) => Err(error.clone()),
                });

            if let Some(done) = done {
                return done;
            }

            std::thread::sleep(Duration::from_millis(10));
        }
    }

    pub fn cancel(&self) {
        let mut inner = self.inner.lock().expect("job lock");

        if inner.outcome.is_none() {
            inner.killed = true;

            if let Some(child) = inner.child.as_mut() {
                terminate_tree(child);
            }
        }
    }

    pub fn child_id(&self) -> Option<u32> {
        let inner = self.inner.lock().expect("job lock");

        inner.child.as_ref().map(Child::id)
    }

    pub fn lines_seen(&self) -> u64 {
        *self.lines_seen.lock().expect("lines lock")
    }

    pub fn stderr_tail(&self, max_bytes: usize) -> String {
        let inner = self.inner.lock().expect("job lock");
        let kept = inner.stderr_bytes.lock().expect("stderr lock");

        let tail = if kept.len() > max_bytes {
            &kept[kept.len() - max_bytes..]
        } else {
            &kept[..]
        };

        String::from_utf8_lossy(tail).into_owned()
    }

    fn settled(&self) -> std::sync::MutexGuard<'_, JobInner> {
        let mut inner = self.inner.lock().expect("job lock");

        enforce_deadline(&mut inner);

        self.finalize_exited(&mut inner);

        inner
    }

    fn finalize_exited(&self, inner: &mut JobInner) {
        if inner.outcome.is_some() {
            return;
        }

        let waiting = inner
            .child
            .as_mut()
            .and_then(|child| child.try_wait().ok().flatten());

        let Some(status) = waiting else {
            return;
        };

        let stderr = settle_child(inner);

        let stderr = String::from_utf8_lossy(&stderr).into_owned();

        if inner.killed {
            cleanup_cancelled(&inner.target);

            let note = if inner.timed_out {
                "yt-dlp download timed out".to_owned()
            } else {
                "yt-dlp download cancelled".to_owned()
            };

            inner.outcome = Some(Outcome::Failed(Error::new(Kind::Cancelled, note)));

            return;
        }

        if status.success() {
            let outcome = match read_info_json(&inner.target) {
                Ok(info_json) => Outcome::Done(info_json),
                Err(failed) => Outcome::Failed(failed),
            };

            inner.outcome = Some(outcome);

            return;
        }

        inner.outcome = Some(Outcome::Failed(classify_stderr(
            &stderr,
            &status.to_string(),
        )));
    }
}

fn state_of(error: &Error) -> JobState {
    if error.kind() == Kind::Cancelled {
        JobState::Cancelled
    } else {
        JobState::Failed
    }
}

fn settle_child(inner: &mut JobInner) -> Vec<u8> {
    if let Some(mut child) = inner.child.take() {
        let _ = child.wait();
    }

    if let Some(pump) = inner.stdout_pump.take() {
        let _ = pump.join();
    }

    if let Some(pump) = inner.stderr_pump.take() {
        let _ = pump.join();
    }

    inner.stderr_bytes.lock().expect("stderr lock").clone()
}

impl Drop for ProcessDownloadJob {
    fn drop(&mut self) {
        self.cancel();

        let Ok(mut inner) = self.inner.lock() else {
            return;
        };

        let _ = settle_child(&mut inner);
    }
}

unsafe impl Send for ProcessDownloadJob {}

unsafe impl Sync for ProcessDownloadJob {}

#[derive(Debug)]
struct ParsedOptions {
    outtmpl: String,
    proxy: Option<String>,
}

fn check_text(field: &str, value: &str) -> Result<(), Error> {
    if value.as_bytes().contains(&0) {
        return Err(Error::new(
            Kind::InvalidArgument,
            format!("{field} must not contain NUL bytes"),
        ));
    }

    Ok(())
}

fn parse_options(options: &str) -> Result<ParsedOptions, Error> {
    let parsed: serde_json::Value = serde_json::from_str(options)
        .map_err(|failed| Error::new(Kind::InvalidArgument, failed.to_string()))?;

    let table = parsed
        .as_object()
        .ok_or_else(|| Error::new(Kind::InvalidArgument, "options must be a JSON object"))?;

    let mut outtmpl: Option<String> = None;

    let mut proxy: Option<String> = None;

    for (key, value) in table {
        if key == "outtmpl" {
            let template = value
                .as_str()
                .ok_or_else(|| Error::new(Kind::InvalidArgument, "outtmpl must be a string"))?;

            if template.is_empty() {
                return Err(Error::new(
                    Kind::InvalidArgument,
                    "outtmpl must not be empty",
                ));
            }

            if template.contains("%(") {
                return Err(Error::new(
                    Kind::InvalidArgument,
                    "process outtmpl must be a literal path, template fields are unsupported",
                ));
            }

            outtmpl = Some(template.to_owned());
        } else if key == "proxy" {
            let target = value
                .as_str()
                .ok_or_else(|| Error::new(Kind::InvalidArgument, "proxy must be a string"))?;

            proxy = Some(target.to_owned());
        } else {
            return Err(Error::new(
                Kind::InvalidArgument,
                format!("unsupported process option: {key}"),
            ));
        }
    }

    let outtmpl = outtmpl.ok_or_else(|| {
        Error::new(
            Kind::InvalidArgument,
            "process downloads need an explicit outtmpl",
        )
    })?;

    Ok(ParsedOptions { outtmpl, proxy })
}

fn argv(options: &ParsedOptions, url: &str) -> Vec<String> {
    let mut args = vec![
        "--newline".to_owned(),
        "--progress".to_owned(),
        "--progress-template".to_owned(),
        PROGRESS_TEMPLATE.to_owned(),
        "--write-info-json".to_owned(),
        "-o".to_owned(),
        options.outtmpl.clone(),
    ];

    if let Some(proxy) = options.proxy.as_deref().filter(|proxy| !proxy.is_empty()) {
        args.push("--proxy".to_owned());

        args.push(proxy.to_owned());
    }

    // SECURITY: everything past here is positional, so a hostile URL cannot parse as a flag.
    args.push("--".to_owned());

    args.push(url.to_owned());

    args
}

fn resolve_executable(config: &ProcessConfig) -> Result<PathBuf, Error> {
    if let Some(wanted) = &config.executable {
        if wanted.as_os_str().is_empty() {
            return Err(Error::new(
                Kind::InvalidArgument,
                "explicit yt-dlp path must not be empty",
            ));
        }

        if !wanted.is_file() {
            return Err(Error::new(
                Kind::InvalidArgument,
                format!("explicit yt-dlp executable not found: {}", wanted.display()),
            ));
        }

        return Ok(wanted.clone());
    }

    let names: &[&str] = if cfg!(windows) {
        &["yt-dlp.exe", "yt-dlp"]
    } else {
        &["yt-dlp"]
    };

    if let Some(path) = std::env::var_os("PATH") {
        for dir in std::env::split_paths(&path) {
            if dir.as_os_str().is_empty() {
                continue;
            }

            for name in names {
                let candidate = dir.join(name);

                if candidate.is_file() {
                    return Ok(candidate);
                }
            }
        }
    }

    Err(Error::new(
        Kind::Runtime,
        "yt-dlp executable not found on PATH; install yt-dlp or set an explicit path",
    ))
}

fn spawn(executable: &Path, args: &[String], direct: bool) -> Result<Child, Error> {
    let mut command = Command::new(executable);

    command
        .args(args)
        .stdin(Stdio::null())
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .env("PYTHONUNBUFFERED", "1");

    if direct {
        for name in PROXY_ENV {
            command.env_remove(name);
        }
    }

    #[cfg(unix)]
    {
        use std::os::unix::process::CommandExt;

        command.process_group(0);
    }

    command.spawn().map_err(|failed| {
        Error::new(
            Kind::Runtime,
            format!(
                "could not start yt-dlp ({}): {failed}",
                executable.display()
            ),
        )
    })
}

fn terminate_tree(child: &mut Child) {
    kill_group(child.id());

    let _ = child.kill();
}

#[cfg(unix)]
fn kill_group(pid: u32) {
    let _ = Command::new("kill")
        .arg("-KILL")
        .arg(format!("-{pid}"))
        .output();
}

#[cfg(windows)]
fn kill_group(pid: u32) {
    let _ = Command::new("taskkill")
        .args(["/PID", &pid.to_string(), "/T", "/F"])
        .output();
}

#[cfg(not(any(unix, windows)))]
fn kill_group(_pid: u32) {}

fn spawn_pumps(
    stdout: std::process::ChildStdout,
    stderr: std::process::ChildStderr,
    events: Arc<Mutex<Vec<ProgressEvent>>>,
    stderr_bytes: Arc<Mutex<Vec<u8>>>,
    lines_seen: Arc<Mutex<u64>>,
) -> (std::thread::JoinHandle<()>, std::thread::JoinHandle<()>) {
    let progress = std::thread::spawn(move || {
        let mut reader = std::io::BufReader::new(stdout);
        let mut line = Vec::new();

        loop {
            line.clear();

            let count = match reader.read_until(b'\n', &mut line) {
                Ok(0) | Err(_) => break,
                Ok(count) => count,
            };

            if count == 0 {
                break;
            }

            if let Ok(mut seen) = lines_seen.lock() {
                *seen += 1;
            }

            let text = String::from_utf8_lossy(&line);

            let Some(begin) = text.find(PROGRESS_PREFIX) else {
                continue;
            };

            if let Some(event) = parse_progress_line(&text[begin + PROGRESS_PREFIX.len()..]) {
                if let Ok(mut queued) = events.lock() {
                    queued.push(event);
                }
            }
        }
    });

    let errors = std::thread::spawn(move || {
        let mut stderr = stderr;

        let mut chunk = [0u8; 8192];

        loop {
            let count = match stderr.read(&mut chunk) {
                Ok(0) | Err(_) => break,
                Ok(count) => count,
            };

            if let Ok(mut kept) = stderr_bytes.lock() {
                kept.extend_from_slice(&chunk[..count]);

                let overflow = kept.len().saturating_sub(STDERR_CAP);

                if overflow > 0 {
                    kept.drain(..overflow);
                }
            }
        }
    });

    (progress, errors)
}

fn parse_progress_line(line: &str) -> Option<ProgressEvent> {
    let table = progress_table(line)?;

    let mut event = serde_json::Map::new();

    event.insert(
        "status".to_owned(),
        serde_json::Value::String("downloading".to_owned()),
    );

    for key in ["downloaded_bytes", "total_bytes", "total_bytes_estimate"] {
        let bytes = table
            .get(key)
            .and_then(serde_json::Value::as_str)
            .and_then(|text| text.parse::<u64>().ok());

        if let Some(bytes) = bytes {
            event.insert(key.to_owned(), serde_json::Value::from(bytes));
        }
    }

    let filename = table
        .get("filename")
        .and_then(serde_json::Value::as_str)
        .filter(|name| *name != "NA" && !name.is_empty());

    if let Some(filename) = filename {
        event.insert(
            "filename".to_owned(),
            serde_json::Value::String(filename.to_owned()),
        );
    }

    Some(ProgressEvent::from_value(serde_json::Value::Object(event)))
}

fn progress_table(line: &str) -> Option<serde_json::Map<String, serde_json::Value>> {
    if let Ok(value) = serde_json::from_str::<serde_json::Value>(line) {
        return value.as_object().cloned();
    }

    scan_progress_table(line)
}

fn scan_progress_table(line: &str) -> Option<serde_json::Map<String, serde_json::Value>> {
    let mut table = serde_json::Map::new();

    for key in ["downloaded_bytes", "total_bytes", "total_bytes_estimate"] {
        if let Some(text) = scan_field(line, key, false) {
            table.insert(key.to_owned(), serde_json::Value::String(text.to_owned()));
        }
    }

    if let Some(name) = scan_field(line, "filename", true) {
        table.insert(
            "filename".to_owned(),
            serde_json::Value::String(name.to_owned()),
        );
    }

    if table.is_empty() { None } else { Some(table) }
}

fn scan_field<'line>(line: &'line str, key: &str, last: bool) -> Option<&'line str> {
    let needle = format!("\"{key}\": \"");

    let begin = line.find(needle.as_str())? + needle.len();

    let rest = &line[begin..];

    let end = if last {
        let trimmed = line.trim_end();

        let object = trimmed.strip_suffix('}').unwrap_or(trimmed);

        let closing = object.rfind('"')?;

        if closing < begin {
            return None;
        }

        closing - begin
    } else {
        rest.find('"')?
    };

    Some(&rest[..end])
}

fn read_info_json(target: &Path) -> Result<String, Error> {
    if !target.is_file() {
        return Err(Error::new(
            Kind::Internal,
            format!("yt-dlp finished but {} is missing", target.display()),
        ));
    }

    let info = PathBuf::from(format!("{}.info.json", target.display()));

    std::fs::read_to_string(&info).map_err(|failed| Error::new(Kind::Internal, failed.to_string()))
}

fn cleanup_cancelled(target: &Path) {
    let _ = std::fs::remove_file(PathBuf::from(format!("{}.part", target.display())));

    let _ = std::fs::remove_file(PathBuf::from(format!("{}.info.json", target.display())));

    let _ = std::fs::remove_file(target);
}

fn classify_stderr(stderr: &str, status_note: &str) -> Error {
    let last = stderr
        .lines()
        .rev()
        .find(|line| !line.trim().is_empty())
        .unwrap_or_default()
        .trim()
        .to_owned();

    let message = if last.is_empty() {
        format!("yt-dlp failed with status {status_note}")
    } else if last.len() > 400 {
        format!("{}...", last.chars().take(400).collect::<String>())
    } else {
        last
    };

    // NOTE: the child only leaves text behind, so transport sniffing is a heuristic, not a contract.
    if TRANSPORT_MARKERS
        .iter()
        .any(|marker| stderr.contains(marker))
    {
        return Error::new(Kind::Network, message).with_retryable(true);
    }

    Error::new(Kind::Extract, message)
}

fn enforce_deadline(inner: &mut JobInner) {
    let expired = inner
        .deadline
        .is_some_and(|deadline| Instant::now() >= deadline);

    if expired && inner.outcome.is_none() {
        inner.killed = true;

        inner.timed_out = true;

        if let Some(child) = inner.child.as_mut() {
            terminate_tree(child);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn progress_line_reads_bytes_and_name() {
        let event = parse_progress_line(
            r#"{"downloaded_bytes": "1024", "total_bytes": "2048", "total_bytes_estimate": "NA", "filename": "/tmp/out.bin"}"#,
        )
        .expect("valid progress must parse");

        assert_eq!(event.status(), Some("downloading"));
        assert_eq!(event.downloaded_bytes(), Some(1024));
        assert_eq!(event.total_bytes(), Some(2048));
        assert_eq!(event.filename(), Some("/tmp/out.bin"));

        let missing = parse_progress_line(
            r#"{"downloaded_bytes": "NA", "total_bytes": "NA", "total_bytes_estimate": "4096", "filename": "NA"}"#,
        )
        .expect("NA fields must parse");

        assert_eq!(missing.downloaded_bytes(), None);
        assert_eq!(missing.total_bytes(), Some(4096));
        assert_eq!(missing.filename(), None);

        assert!(parse_progress_line("not json").is_none());
    }

    #[test]
    fn progress_line_with_windows_path_scans_fields() {
        let event = parse_progress_line(
            "{\"status\": \"downloading\", \"downloaded_bytes\": \"8788599\", \"total_bytes\": \"None\", \"total_bytes_estimate\": \"32000000\", \"filename\": \"C:\\Users\\RUNNER~1\\Temp\\out.bin\"}\n",
        )
        .expect("windows path must parse");

        assert_eq!(event.status(), Some("downloading"));
        assert_eq!(event.downloaded_bytes(), Some(8788599));
        assert_eq!(event.total_bytes(), Some(32000000));
        assert_eq!(event.filename(), Some("C:\\Users\\RUNNER~1\\Temp\\out.bin"));
    }

    #[test]
    fn stderr_classification_splits_transport_from_extract() {
        let network = classify_stderr(
            "ERROR: [generic] file: Unable to download webpage: Connection refused",
            "exit status: 1",
        );

        assert_eq!(network.kind(), Kind::Network);
        assert!(network.is_retryable());

        let extract = classify_stderr(
            "ERROR: Unable to handle request: Unsupported url scheme: \"ly-bogus\"",
            "exit status: 1",
        );

        assert_eq!(extract.kind(), Kind::Extract);
        assert!(!extract.is_retryable());
    }

    #[test]
    fn options_reject_unknown_keys_and_missing_outtmpl() {
        let parsed = parse_options(r#"{"outtmpl": "/tmp/x.bin", "proxy": ""}"#)
            .expect("supported options must parse");

        assert_eq!(parsed.outtmpl, "/tmp/x.bin");
        assert_eq!(parsed.proxy.as_deref(), Some(""));

        for bad in [
            r#"{"outtmpl": "/tmp/x.bin", "format": "best"}"#,
            r#"{"outtmpl": "/tmp/%(title)s.bin"}"#,
            "{}",
        ] {
            let failed = parse_options(bad).expect_err("bad options must fail");

            assert_eq!(failed.kind(), Kind::InvalidArgument);
        }
    }
}
