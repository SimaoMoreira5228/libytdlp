#include <libytdlp/libytdlp.h>

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <memory>
#include <mutex>
#include <new>
#include <string>

#include "bridge.hpp"
#include "install_dirs.hpp"
#include "jobs.hpp"
#include "runtime.hpp"

namespace {

struct LyContext {
    std::string options = "{}";
};

struct LyResult {
    std::string json;
};

struct LyError {
    ly_status code = LY_E_INTERNAL;
    std::string message;
    std::string exception_class;
    mutable std::string cached_details;
    mutable std::mutex details_mutex;
};

std::string installed_share_dir() {
    return libytdlp::staged_share_dir();
}

std::string bridge_dir() {
    const char* from_env = std::getenv("LIBYTDLP_BRIDGE_DIR");
    if (from_env != nullptr) {
        return from_env;
    }

    std::string share = installed_share_dir();

    if (!share.empty()) {
        std::filesystem::path bridge = std::filesystem::path(share) / "python";

        return bridge.string();
    }

#ifdef LIBYTDLP_BRIDGE_DIR
    return LIBYTDLP_BRIDGE_DIR;
#else
    return "";
#endif
}

std::string ytdlp_dir() {
    const char* from_env = std::getenv("LIBYTDLP_YTDLP_DIR");
    if (from_env != nullptr) {
        return from_env;
    }

    std::string share = installed_share_dir();

    if (!share.empty()) {
        std::filesystem::path ytdlp = std::filesystem::path(share) / "yt-dlp";

        return ytdlp.string();
    }

#ifdef LIBYTDLP_YTDLP_DIR
    return LIBYTDLP_YTDLP_DIR;
#else
    return "";
#endif
}

std::unique_ptr<LyError> make_error(
    ly_status code,
    const std::string& message,
    const std::string& exception_class = std::string()
) {
    auto error = std::make_unique<LyError>();
    error->code = code;
    error->message = message;
    error->exception_class = exception_class;
    return error;
}

bool store_error(ly_error** error, std::unique_ptr<LyError> owned) {
    if (error == nullptr) {
        return false;
    }

    *error = reinterpret_cast<ly_error*>(owned.release());
    return true;
}

bool report(ly_error** error, ly_status code, const char* message) noexcept {
    if (error == nullptr) {
        return false;
    }

    auto* slot = new (std::nothrow) LyError();
    if (slot == nullptr) {
        *error = nullptr;
        return false;
    }

    try {
        slot->message = message;
    } catch (...) {
        delete slot;
        *error = nullptr;
        return false;
    }

    slot->code = code;
    *error = reinterpret_cast<ly_error*>(slot);
    return true;
}

bool ensure_runtime(ly_error** error) {
    std::string message;
    std::string bridge = bridge_dir();
    std::string ytdlp = ytdlp_dir();
    if (libytdlp::Runtime::instance().ensure(bridge.c_str(), ytdlp.c_str(), message)) {
        return true;
    }

    store_error(error, make_error(LY_E_RUNTIME, message));
    return false;
}

const char* status_kind(ly_status status) {
    switch (status) {
    case LY_OK:
        return "ok";
    case LY_E_INVALID_ARGUMENT:
        return "invalid_argument";
    case LY_E_RUNTIME:
        return "runtime";
    case LY_E_EXTRACT:
        return "extract";
    case LY_E_NETWORK:
        return "network";
    case LY_E_CANCELLED:
        return "cancelled";
    case LY_E_BUSY:
        return "busy";
    case LY_E_INTERNAL:
        return "internal";
    }

    return "internal";
}

void append_escaped(std::string& out, const std::string& text) {
    static const char* kHex = "0123456789abcdef";
    out += '"';

    for (char step : text) {
        unsigned char c = static_cast<unsigned char>(step);

        if (c == '"' || c == '\\') {
            out += '\\';
            out += static_cast<char>(c);
            continue;
        }

        if (c < 0x20) {
            out += "\\u00";
            out += kHex[c >> 4];
            out += kHex[c & 0x0F];
            continue;
        }

        out += static_cast<char>(c);
    }

    out += '"';
}

} // namespace

uint32_t ly_abi_version(void) noexcept {
    return LIBYTDLP_ABI_VERSION;
}

ly_status ly_context_create(
    const char* options_json,
    size_t options_len,
    ly_context** out,
    ly_error** error
) noexcept {
    try {
        if (out == nullptr) {
            store_error(error, make_error(LY_E_INVALID_ARGUMENT, "out must not be null"));
            return LY_E_INVALID_ARGUMENT;
        }
        *out = nullptr;

        if (error != nullptr) {
            *error = nullptr;
        }

        if (options_json == nullptr && options_len != 0) {
            store_error(error, make_error(LY_E_INVALID_ARGUMENT, "options length needs a buffer"));
            return LY_E_INVALID_ARGUMENT;
        }

        auto context = std::make_unique<LyContext>();
        if (options_json != nullptr && options_len != 0) {
            context->options.assign(options_json, options_len);
        }

        *out = reinterpret_cast<ly_context*>(context.release());
        return LY_OK;
    } catch (const std::exception& caught) {
        report(error, LY_E_INTERNAL, caught.what());
        return LY_E_INTERNAL;
    } catch (...) {
        report(error, LY_E_INTERNAL, "unknown context creation failure");
        return LY_E_INTERNAL;
    }
}

ly_status ly_context_release(ly_context* context) noexcept {
    if (context == nullptr) {
        return LY_OK;
    }

    delete reinterpret_cast<LyContext*>(context);
    return LY_OK;
}

ly_status ly_extract(
    ly_context* context,
    const char* url,
    size_t url_len,
    const char* options_json,
    size_t options_len,
    ly_result** out,
    ly_error** error
) noexcept {
    try {
        if (out == nullptr) {
            store_error(error, make_error(LY_E_INVALID_ARGUMENT, "out must not be null"));
            return LY_E_INVALID_ARGUMENT;
        }
        *out = nullptr;

        if (error != nullptr) {
            *error = nullptr;
        }

        if (context == nullptr) {
            store_error(error, make_error(LY_E_INVALID_ARGUMENT, "context must not be null"));
            return LY_E_INVALID_ARGUMENT;
        }

        if (url == nullptr || url_len == 0) {
            store_error(error, make_error(LY_E_INVALID_ARGUMENT, "url must not be empty"));
            return LY_E_INVALID_ARGUMENT;
        }

        if (options_json == nullptr && options_len != 0) {
            store_error(error, make_error(LY_E_INVALID_ARGUMENT, "options length needs a buffer"));
            return LY_E_INVALID_ARGUMENT;
        }

        if (!ensure_runtime(error)) {
            return LY_E_RUNTIME;
        }

        std::string url_text(url, url_len);
        const std::string& context_options = reinterpret_cast<LyContext*>(context)->options;

        std::string call_options = "{}";

        if (options_json != nullptr && options_len != 0) {
            call_options.assign(options_json, options_len);
        }

        libytdlp::BridgeResult called =
            libytdlp::extract_json(url_text, context_options, call_options);
        if (!called.ok) {
            store_error(error, make_error(called.status, called.message, called.exception_class));
            return called.status;
        }

        auto result = std::make_unique<LyResult>();
        result->json = std::move(called.json);

        *out = reinterpret_cast<ly_result*>(result.release());
        return LY_OK;
    } catch (const std::exception& caught) {
        report(error, LY_E_INTERNAL, caught.what());
        return LY_E_INTERNAL;
    } catch (...) {
        report(error, LY_E_INTERNAL, "unknown extraction failure");
        return LY_E_INTERNAL;
    }
}

ly_status ly_version_info(ly_result** out, ly_error** error) noexcept {
    try {
        if (out == nullptr) {
            store_error(error, make_error(LY_E_INVALID_ARGUMENT, "out must not be null"));
            return LY_E_INVALID_ARGUMENT;
        }
        *out = nullptr;

        if (error != nullptr) {
            *error = nullptr;
        }

        if (!ensure_runtime(error)) {
            return LY_E_RUNTIME;
        }

        libytdlp::BridgeResult called = libytdlp::runtime_info_json();
        if (!called.ok) {
            store_error(error, make_error(called.status, called.message, called.exception_class));
            return called.status;
        }

        auto result = std::make_unique<LyResult>();
        result->json = std::move(called.json);

        *out = reinterpret_cast<ly_result*>(result.release());
        return LY_OK;
    } catch (const std::exception& caught) {
        report(error, LY_E_INTERNAL, caught.what());
        return LY_E_INTERNAL;
    } catch (...) {
        report(error, LY_E_INTERNAL, "unknown version query failure");
        return LY_E_INTERNAL;
    }
}

ly_status ly_result_json(const ly_result* result, const char** data, size_t* len) noexcept {
    if (result == nullptr || data == nullptr || len == nullptr) {
        return LY_E_INVALID_ARGUMENT;
    }

    const std::string& json = reinterpret_cast<const LyResult*>(result)->json;
    *data = json.data();
    *len = json.size();
    return LY_OK;
}

void ly_result_release(ly_result* result) noexcept {
    delete reinterpret_cast<LyResult*>(result);
}

ly_status ly_download_start(
    ly_context* context,
    const char* url,
    size_t url_len,
    const char* options_json,
    size_t options_len,
    ly_job** out,
    ly_error** error
) noexcept {
    try {
        if (out == nullptr) {
            store_error(error, make_error(LY_E_INVALID_ARGUMENT, "out must not be null"));
            return LY_E_INVALID_ARGUMENT;
        }
        *out = nullptr;

        if (error != nullptr) {
            *error = nullptr;
        }

        if (context == nullptr) {
            store_error(error, make_error(LY_E_INVALID_ARGUMENT, "context must not be null"));
            return LY_E_INVALID_ARGUMENT;
        }

        if (url == nullptr || url_len == 0) {
            store_error(error, make_error(LY_E_INVALID_ARGUMENT, "url must not be empty"));
            return LY_E_INVALID_ARGUMENT;
        }

        if (options_json == nullptr && options_len != 0) {
            store_error(error, make_error(LY_E_INVALID_ARGUMENT, "options length needs a buffer"));
            return LY_E_INVALID_ARGUMENT;
        }

        if (!ensure_runtime(error)) {
            return LY_E_RUNTIME;
        }

        std::string url_text(url, url_len);
        std::string options = reinterpret_cast<LyContext*>(context)->options;
        std::string job_options = "{}";

        if (options_json != nullptr && options_len != 0) {
            job_options.assign(options_json, options_len);
        }

        std::string failure;
        libytdlp::DownloadJob* job = libytdlp::DownloadJob::create(
            std::move(url_text),
            std::move(options),
            std::move(job_options),
            failure
        );
        if (job == nullptr) {
            store_error(error, make_error(LY_E_RUNTIME, failure));
            return LY_E_RUNTIME;
        }

        *out = reinterpret_cast<ly_job*>(job);
        return LY_OK;
    } catch (const std::exception& caught) {
        report(error, LY_E_INTERNAL, caught.what());
        return LY_E_INTERNAL;
    } catch (...) {
        report(error, LY_E_INTERNAL, "unknown download start failure");
        return LY_E_INTERNAL;
    }
}

ly_status
ly_job_poll(ly_job* job, ly_job_state* state, ly_result** events, ly_error** error) noexcept {
    try {
        if (events == nullptr) {
            store_error(error, make_error(LY_E_INVALID_ARGUMENT, "events must not be null"));
            return LY_E_INVALID_ARGUMENT;
        }
        *events = nullptr;

        if (error != nullptr) {
            *error = nullptr;
        }

        if (job == nullptr) {
            store_error(error, make_error(LY_E_INVALID_ARGUMENT, "job must not be null"));
            return LY_E_INVALID_ARGUMENT;
        }

        if (state == nullptr) {
            store_error(error, make_error(LY_E_INVALID_ARGUMENT, "state must not be null"));
            return LY_E_INVALID_ARGUMENT;
        }

        std::string events_json;
        ly_job_state current = reinterpret_cast<libytdlp::DownloadJob*>(job)->poll(events_json);

        auto result = std::make_unique<LyResult>();
        result->json = std::move(events_json);

        *events = reinterpret_cast<ly_result*>(result.release());
        *state = current;
        return LY_OK;
    } catch (const std::exception& caught) {
        report(error, LY_E_INTERNAL, caught.what());
        return LY_E_INTERNAL;
    } catch (...) {
        report(error, LY_E_INTERNAL, "unknown job poll failure");
        return LY_E_INTERNAL;
    }
}

ly_status ly_job_wait(ly_job* job, ly_result** out, ly_error** error) noexcept {
    try {
        if (out == nullptr) {
            store_error(error, make_error(LY_E_INVALID_ARGUMENT, "out must not be null"));
            return LY_E_INVALID_ARGUMENT;
        }
        *out = nullptr;

        if (error != nullptr) {
            *error = nullptr;
        }

        if (job == nullptr) {
            store_error(error, make_error(LY_E_INVALID_ARGUMENT, "job must not be null"));
            return LY_E_INVALID_ARGUMENT;
        }

        std::string json;
        ly_status status = LY_OK;
        std::string message;
        std::string exception_class;
        reinterpret_cast<libytdlp::DownloadJob*>(job)->wait(json, status, message, exception_class);

        if (status != LY_OK) {
            store_error(error, make_error(status, message, exception_class));
            return status;
        }

        auto result = std::make_unique<LyResult>();
        result->json = std::move(json);

        *out = reinterpret_cast<ly_result*>(result.release());
        return LY_OK;
    } catch (const std::exception& caught) {
        report(error, LY_E_INTERNAL, caught.what());
        return LY_E_INTERNAL;
    } catch (...) {
        report(error, LY_E_INTERNAL, "unknown job wait failure");
        return LY_E_INTERNAL;
    }
}

ly_status ly_job_cancel(ly_job* job) noexcept {
    try {
        if (job == nullptr) {
            return LY_E_INVALID_ARGUMENT;
        }

        reinterpret_cast<libytdlp::DownloadJob*>(job)->cancel();
        return LY_OK;
    } catch (...) {
        return LY_E_INTERNAL;
    }
}

void ly_job_release(ly_job* job) noexcept {
    delete reinterpret_cast<libytdlp::DownloadJob*>(job);
}

ly_status ly_error_code(const ly_error* error) noexcept {
    if (error == nullptr) {
        return LY_E_INVALID_ARGUMENT;
    }

    return reinterpret_cast<const LyError*>(error)->code;
}

const char* ly_error_message(const ly_error* error) noexcept {
    if (error == nullptr) {
        return nullptr;
    }

    return reinterpret_cast<const LyError*>(error)->message.c_str();
}

bool ly_error_is_retryable(const ly_error* error) noexcept {
    if (error == nullptr) {
        return false;
    }

    switch (reinterpret_cast<const LyError*>(error)->code) {
    case LY_E_NETWORK:
    case LY_E_BUSY:
        return true;
    default:
        return false;
    }
}

void ly_error_release(ly_error* error) noexcept {
    delete reinterpret_cast<LyError*>(error);
}

ly_status ly_error_details_json(const ly_error* error, const char** data, size_t* len) noexcept {
    try {
        if (error == nullptr || data == nullptr || len == nullptr) {
            return LY_E_INVALID_ARGUMENT;
        }

        const LyError* owned = reinterpret_cast<const LyError*>(error);
        std::lock_guard<std::mutex> details_lock(owned->details_mutex);
        std::string json = "{\"code\":";

        json += std::to_string(static_cast<unsigned int>(owned->code));
        json += ",\"kind\":\"";
        json += status_kind(owned->code);
        json += "\",\"class\":";

        if (owned->exception_class.empty()) {
            json += "null";
        } else {
            append_escaped(json, owned->exception_class);
        }

        json += ",\"message\":";
        append_escaped(json, owned->message);
        json += "}";

        owned->cached_details = std::move(json);
        *data = owned->cached_details.data();
        *len = owned->cached_details.size();
        return LY_OK;
    } catch (...) {
        return LY_E_INTERNAL;
    }
}
