use std::cmp::Ordering;
use std::process::ExitCode;

use libytdlp::Client;
use libytdlp::Format;
use libytdlp::MediaInfo;

fn is_audio_only(format: &Format) -> bool {
    let has_audio = format.acodec().is_some_and(|codec| codec != "none");

    let has_video = format.vcodec().is_some_and(|codec| codec != "none");

    has_audio && !has_video
}

fn by_bitrate(left: &Format, right: &Format) -> Ordering {
    left.abr()
        .partial_cmp(&right.abr())
        .unwrap_or(Ordering::Equal)
}

fn pick_stream(info: &MediaInfo) -> Option<String> {
    let formats = info.formats();

    let best = formats
        .iter()
        .filter(|format| is_audio_only(format) && format.url().is_some())
        .max_by(|left, right| by_bitrate(left, right));

    let fallback = formats.iter().find(|format| format.url().is_some());

    if let Some(format) = best.or(fallback) {
        if let Some(url) = format.url() {
            return Some(url.to_owned());
        }
    }

    info.value()
        .get("url")
        .and_then(|value| value.as_str())
        .map(str::to_owned)
}

fn run(url: &str) -> Result<(), libytdlp::Error> {
    let client = Client::new("{}")?;

    let info = client.extract(url)?;

    let title = info.title().unwrap_or("unknown");

    let duration = match info.duration_secs() {
        Some(secs) => secs.to_string(),
        None => "unknown".to_owned(),
    };

    let uploader = info.uploader().unwrap_or("unknown");

    let stream = match pick_stream(&info) {
        Some(url) => url,
        None => "unknown".to_owned(),
    };

    println!("title={title}");
    println!("duration_secs={duration}");
    println!("uploader={uploader}");
    println!("url={stream}");

    Ok(())
}

fn main() -> ExitCode {
    let mut args = std::env::args();

    let _ = args.next();

    let Some(url) = args.next() else {
        eprintln!("usage: resolve <url>");

        return ExitCode::FAILURE;
    };

    if let Err(error) = run(&url) {
        eprintln!("error: {error}");

        return ExitCode::FAILURE;
    }

    ExitCode::SUCCESS
}
