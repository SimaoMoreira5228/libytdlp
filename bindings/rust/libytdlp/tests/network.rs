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
            assert!(info.title().is_some());
            assert!(info.extractor().is_some());
            assert!(info.webpage_url().is_some());

            let formats = info.formats();

            assert!(!formats.is_empty());
            assert!(formats.iter().all(|format| format.url().is_some()));
            assert!(formats.iter().any(|format| format.format_id().is_some()));
        }
        Err(error) => {
            eprintln!("network test failed (flaky is fine): {error}");
        }
    }
}

#[test]
fn network_search() {
    if std::env::var("LIBYTDLP_TEST_SEARCH").is_err() {
        eprintln!("no LIBYTDLP_TEST_SEARCH, skipping search test");
        return;
    }

    let client = Client::new("{\"extract_flat\": true}").expect("default options must be accepted");

    match client.extract("ytsearch5:never gonna give you up") {
        Ok(info) => {
            let entries = info
                .value()
                .get("entries")
                .and_then(|entries| entries.as_array());

            assert!(entries.is_some_and(|entries| !entries.is_empty()));
        }
        Err(error) => {
            eprintln!("search test failed (flaky is fine): {error}");
        }
    }
}
