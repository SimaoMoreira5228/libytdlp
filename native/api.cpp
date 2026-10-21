#include <libytdlp/libytdlp.h>

#include <cstdlib>
#include <exception>
#include <memory>
#include <new>
#include <string>

#include "bridge.hpp"
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
};

std::string bridge_dir() {
    const char* from_env = std::getenv("LIBYTDLP_BRIDGE_DIR");
    if (from_env != nullptr) {
        return from_env;
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

#ifdef LIBYTDLP_YTDLP_DIR
    return LIBYTDLP_YTDLP_DIR;
#else
    return "";
#endif
}

std::unique_ptr<LyError> make_error(ly_status code, const std::string& message) {
    auto error = std::make_unique<LyError>();
    error->code = code;
    error->message = message;
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

        if (!ensure_runtime(error)) {
            return LY_E_RUNTIME;
        }

        std::string url_text(url, url_len);
        const std::string& options = reinterpret_cast<LyContext*>(context)->options;

        libytdlp::BridgeResult called = libytdlp::extract_json(url_text, options);
        if (!called.ok) {
            store_error(error, make_error(called.status, called.message));
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

void ly_error_release(ly_error* error) noexcept {
    delete reinterpret_cast<LyError*>(error);
}
