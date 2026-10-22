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

fn copy_dir(source: &std::path::Path, target: &std::path::Path) {
    std::fs::create_dir_all(target).expect("copy target must be created");

    for entry in std::fs::read_dir(source).expect("copy source must list") {
        let entry = entry.expect("copy entry must read");
        let name = entry.file_name();
        let from = entry.path();
        let to = target.join(&name);

        if from.is_dir() {
            copy_dir(&from, &to);
        } else {
            std::fs::copy(&from, &to).expect("copy file must succeed");
        }
    }
}

#[test]
fn extract_with_embedded_bridge_and_alt_ytdlp() {
    let server = FixtureServer::start(65536, 8_000_000);

    let alt = std::env::temp_dir().join(format!("libytdlp-alt-{}", std::process::id()));
    let _ = std::fs::remove_dir_all(&alt);

    copy_dir(
        &PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../../third_party/yt-dlp/yt_dlp"),
        &alt.join("yt_dlp"),
    );

    unsafe {
        std::env::set_var("LIBYTDLP_BRIDGE_DIR", "");
        std::env::set_var("LIBYTDLP_YTDLP_DIR", "");
        std::env::set_var("PYTHONPATH", &alt);
    }

    let client = libytdlp::Client::new("{}").expect("client must build");

    let info = client
        .extract(&server.url("/file.bin"))
        .expect("embedded bridge extract must succeed");

    assert!(info.value().is_object());

    match info
        .value()
        .get("formats")
        .and_then(|formats| formats.as_array())
    {
        Some(formats) => assert!(!formats.is_empty()),
        None => assert!(
            info.value()
                .get("url")
                .and_then(|url| url.as_str())
                .is_some_and(|url| url.contains(&server.port.to_string())),
            "direct media URL must point at the fixture"
        ),
    }

    assert!(
        info.webpage_url()
            .is_some_and(|url| url.contains(&server.port.to_string())),
        "extracted URL must point at the fixture"
    );

    let versions = libytdlp::runtime_info().expect("runtime info must succeed");

    assert_eq!(
        versions.value().get("backend").and_then(|v| v.as_str()),
        Some("embedded")
    );

    let _ = std::fs::remove_dir_all(&alt);
}
