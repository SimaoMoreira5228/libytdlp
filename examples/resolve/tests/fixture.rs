use std::collections::HashMap;
use std::io::BufRead;
use std::io::BufReader;
use std::path::PathBuf;
use std::process::Child;
use std::process::Command;
use std::process::Stdio;

struct FixtureServer {
    child: Child,
    port: u16,
}

impl FixtureServer {
    fn start(bytes: u64, rate: u64) -> Self {
        let fixture =
            PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../tests/fixtures/slow_server.py");

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
fn resolves_fixture_stream_offline() {
    let server = FixtureServer::start(65536, 8_000_000);

    let url = server.url("/song.bin");

    let output = Command::new(env!("CARGO_BIN_EXE_resolve"))
        .arg(&url)
        .output()
        .expect("resolve binary must run");

    let stderr = String::from_utf8_lossy(&output.stderr);

    assert!(output.status.success(), "resolve failed: {stderr}");

    let text = String::from_utf8(output.stdout).expect("resolve output must be UTF-8");

    let mut fields = HashMap::new();

    for line in text.lines() {
        let (key, value) = line
            .split_once('=')
            .expect("output must be key=value lines");

        let _ = fields.insert(key.to_owned(), value.to_owned());
    }

    for key in ["title", "duration_secs", "uploader", "url"] {
        assert!(fields.contains_key(key), "missing {key} in:\n{text}");
    }

    let stream = fields.get("url").expect("url must be present");

    assert!(stream.starts_with(&server.url("")), "stream was: {stream}");
}
