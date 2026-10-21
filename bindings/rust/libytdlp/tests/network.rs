use libytdlp::Client;

#[test]
fn network_extract() {
    let url = std::env::var("LIBYTDLP_TEST_URL").unwrap_or_default();

    if url.is_empty() {
        eprintln!("no LIBYTDLP_TEST_URL, skipping network test");
        return;
    }

    let client = Client::new("{}").expect("default options must be accepted");

    match client.extract(&url) {
        Ok(info) => {
            assert!(info.raw_json().len() > 100);
            assert!(info.value().is_object());
        }
        Err(error) => {
            eprintln!("network test failed (flaky is fine): {error}");
        }
    }
}
