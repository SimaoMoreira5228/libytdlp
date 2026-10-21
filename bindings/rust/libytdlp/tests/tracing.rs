use std::fmt;
use std::sync::Arc;
use std::sync::Mutex;
use std::sync::OnceLock;
use std::sync::atomic::AtomicU64;
use std::sync::atomic::Ordering;

use tracing::Level;
use tracing::Metadata;
use tracing::Subscriber;
use tracing::field::Field;
use tracing::field::Visit;
use tracing::span::Attributes;
use tracing::span::Id;
use tracing::span::Record;

struct RecordedEvent {
    level: Level,
    kind: Option<String>,
}

struct EventFields {
    kind: Option<String>,
}

impl Visit for EventFields {
    fn record_str(&mut self, field: &Field, value: &str) {
        if field.name() == "kind" {
            self.kind = Some(value.to_owned());
        }
    }

    fn record_debug(&mut self, _field: &Field, _value: &dyn fmt::Debug) {}
}

struct Inner {
    next_id: AtomicU64,
    events: Mutex<Vec<RecordedEvent>>,
}

#[derive(Clone)]
struct RecordingSubscriber {
    inner: Arc<Inner>,
}

impl RecordingSubscriber {
    fn install() -> Self {
        static CELL: OnceLock<RecordingSubscriber> = OnceLock::new();

        CELL.get_or_init(|| {
            let subscriber = RecordingSubscriber {
                inner: Arc::new(Inner {
                    next_id: AtomicU64::new(1),
                    events: Mutex::new(Vec::new()),
                }),
            };

            let _ = tracing::dispatcher::set_global_default(tracing::dispatcher::Dispatch::new(
                subscriber.clone(),
            ));

            subscriber
        })
        .clone()
    }

    fn clear(&self) {
        self.inner.events.lock().expect("events lock").clear();
    }

    fn kinds(&self) -> Vec<(Level, Option<String>)> {
        self.inner
            .events
            .lock()
            .expect("events lock")
            .iter()
            .map(|event| (event.level, event.kind.clone()))
            .collect()
    }
}

impl Subscriber for RecordingSubscriber {
    fn enabled(&self, metadata: &Metadata<'_>) -> bool {
        metadata.target().starts_with("libytdlp")
    }

    fn new_span(&self, _span: &Attributes<'_>) -> Id {
        Id::from_u64(self.inner.next_id.fetch_add(1, Ordering::Relaxed))
    }

    fn record(&self, _span: &Id, _values: &Record<'_>) {}

    fn record_follows_from(&self, _span: &Id, _follows: &Id) {}

    fn enter(&self, _span: &Id) {}

    fn exit(&self, _span: &Id) {}

    fn drop_span(&self, _id: Id) {}

    fn event(&self, event: &tracing::Event<'_>) {
        if !self.enabled(event.metadata()) {
            return;
        }

        let mut fields = EventFields { kind: None };
        event.record(&mut fields);

        self.inner
            .events
            .lock()
            .expect("events lock")
            .push(RecordedEvent {
                level: *event.metadata().level(),
                kind: fields.kind,
            });
    }

    unsafe fn downcast_raw(&self, id: std::any::TypeId) -> Option<*const ()> {
        if id == std::any::TypeId::of::<Self>() {
            Some(self as *const Self as *const ())
        } else {
            None
        }
    }
}

#[test]
fn failed_extract_emits_kind_only() {
    let subscriber = RecordingSubscriber::install();
    subscriber.clear();

    let client = libytdlp::Client::new("{}").expect("default options must be accepted");

    let error = client
        .extract("ly-bogus://no-such-scheme-xyz")
        .expect_err("bogus url must fail");

    assert!(!error.message().is_empty());

    let recorded = subscriber.kinds();

    assert!(
        recorded
            .iter()
            .any(|(level, kind)| *level == Level::DEBUG && kind.as_deref() == Some("extract")),
        "no extract failure event recorded: {recorded:?}"
    );
}
