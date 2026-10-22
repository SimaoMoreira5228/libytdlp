use std::io::BufRead;
use std::io::BufReader;
use std::path::PathBuf;
use std::pin::Pin;
use std::process::Child;
use std::process::Command;
use std::process::Stdio;
use std::task::Context;
use std::task::Poll;
use std::task::RawWaker;
use std::task::RawWakerVTable;
use std::task::Waker;
use std::time::Duration;

use libytdlp::Client;
use libytdlp::WaitForDownload;
use libytdlp::WaitForExtract;

fn assert_send<T>()
where
    T: Send,
{
}

#[test]
fn download_future_is_send() {
    assert_send::<WaitForDownload>();
    assert_send::<WaitForExtract>();
}

fn block_on<F>(future: F) -> F::Output
where
    F: std::future::Future,
{
    unsafe fn clone_waker(data: *const ()) -> RawWaker {
        RawWaker::new(data, &WAKER_VTABLE)
    }

    unsafe fn wake(data: *const ()) {
        let thread = unsafe { &*(data as *const std::thread::Thread) };

        thread.unpark();
    }

    unsafe fn wake_by_ref(data: *const ()) {
        let thread = unsafe { &*(data as *const std::thread::Thread) };

        thread.unpark();
    }

    unsafe fn drop_waker(_: *const ()) {}

    static WAKER_VTABLE: RawWakerVTable =
        RawWakerVTable::new(clone_waker, wake, wake_by_ref, drop_waker);

    let thread = std::thread::current();

    let raw = RawWaker::new(
        &thread as *const std::thread::Thread as *const (),
        &WAKER_VTABLE,
    );

    let waker = unsafe { Waker::from_raw(raw) };

    let mut context = Context::from_waker(&waker);

    let mut future = std::pin::pin!(future);

    loop {
        match future.as_mut().poll(&mut context) {
            Poll::Ready(output) => return output,
            Poll::Pending => std::thread::park(),
        }
    }
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

fn temp_output(name: &str) -> (PathBuf, String) {
    let dir = std::env::temp_dir().join(format!(
        "libytdlp-asynctest-{}-{}",
        std::process::id(),
        name
    ));

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
fn async_download_completes() {
    let server = FixtureServer::start(65536, 8_000_000);
    let (target, options) = temp_output("success");

    let client = Client::new(&options).expect("options must be accepted");

    let job = client
        // NOTE: force direct connections so the loopback fixture ignores any
        // ambient proxy env; env scrubbing would race across parallel tests.
        .download(&server.url("/file.bin"), "{\"proxy\": \"\"}")
        .expect("job must start");

    let info = block_on(job.wait_async()).expect("async download must succeed");

    assert!(info.raw_json().len() > 100);
    assert!(info.value().is_object());

    let bytes = std::fs::read(&target).expect("output file must exist");

    assert_eq!(bytes.len(), 65536);

    let parent = target.parent().expect("output must have a parent");
    let _ = std::fs::remove_dir_all(parent);
}

#[test]
fn async_extract_completes() {
    let server = FixtureServer::start(65536, 8_000_000);

    // NOTE: force direct connections so the loopback fixture ignores any
    // ambient proxy env; env scrubbing would race across parallel tests.
    let client = Client::new("{\"proxy\": \"\"}").expect("default options must be accepted");

    let info = block_on(client.extract_async(server.url("/file.bin")))
        .expect("async extract must succeed");

    assert!(info.raw_json().len() > 100);
    assert!(info.value().is_object());
    assert_eq!(info.id(), Some("file"));
}

#[test]
fn concurrent_async_extracts_share_client() {
    let server = FixtureServer::start(65536, 8_000_000);
    let url = server.url("/file.bin");

    // NOTE: force direct connections so the loopback fixture ignores any
    // ambient proxy env; env scrubbing would race across parallel tests.
    let client =
        std::sync::Arc::new(Client::new("{\"proxy\": \"\"}").expect("options must be accepted"));

    let handles: Vec<std::thread::JoinHandle<()>> = (0..4)
        .map(|_| {
            let shared = std::sync::Arc::clone(&client);
            let target = url.clone();

            std::thread::spawn(move || {
                let info = block_on(shared.extract_async(target))
                    .expect("concurrent extract must succeed");

                assert_eq!(info.id(), Some("file"));
            })
        })
        .collect();

    for handle in handles {
        handle.join().expect("worker thread must not panic");
    }
}

#[test]
fn dropping_future_mid_flight_is_safe() {
    let server = FixtureServer::start(20_000_000, 200_000);
    let (target, options) = temp_output("drop");

    let client = Client::new(&options).expect("options must be accepted");

    let job = client
        // NOTE: force direct connections so the loopback fixture ignores any
        // ambient proxy env; env scrubbing would race across parallel tests.
        .download(&server.url("/big.bin"), "{\"proxy\": \"\"}")
        .expect("job must start");

    {
        let mut future = job.wait_async();

        let waker = Waker::noop();
        let mut context = Context::from_waker(waker);

        assert!(matches!(
            Pin::new(&mut future).poll(&mut context),
            Poll::Pending
        ));
    }

    drop(server);

    let parent = target.parent().expect("output must have a parent");

    let deadline = std::time::Instant::now() + Duration::from_secs(30);

    while parent.exists() {
        let _ = std::fs::remove_dir_all(parent);

        if parent.exists() {
            assert!(
                std::time::Instant::now() < deadline,
                "temp dir must clean up after drop"
            );

            std::thread::sleep(Duration::from_millis(100));
        }
    }
}
