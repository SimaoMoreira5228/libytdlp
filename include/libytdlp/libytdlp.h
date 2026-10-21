#ifndef LIBYTDLP_H
#define LIBYTDLP_H

#include <stddef.h>
#include <stdint.h>

#include <libytdlp/version.h>

#ifdef __cplusplus
#define LY_NOEXCEPT noexcept
extern "C" {
#else
#define LY_NOEXCEPT
#endif

#if defined(_WIN32) && defined(LY_BUILDING_SHARED)
#define LY_API __declspec(dllexport)
#elif defined(_WIN32)
#define LY_API __declspec(dllimport)
#elif defined(__GNUC__) || defined(__clang__)
#define LY_API __attribute__((visibility("default")))
#else
#define LY_API
#endif

typedef struct ly_context ly_context;
typedef struct ly_result ly_result;
typedef struct ly_error ly_error;

typedef enum ly_status {
    LY_OK = 0,
    LY_E_INVALID_ARGUMENT = 1,
    LY_E_RUNTIME = 2,
    LY_E_EXTRACT = 3,
    LY_E_NETWORK = 4,
    LY_E_CANCELLED = 5,
    LY_E_BUSY = 6,
    LY_E_INTERNAL = 7
} ly_status;

LY_API uint32_t ly_abi_version(void) LY_NOEXCEPT;

LY_API ly_status ly_context_create(
    const char* options_json,
    size_t options_len,
    ly_context** out,
    ly_error** error
) LY_NOEXCEPT;

LY_API ly_status ly_context_release(ly_context* context) LY_NOEXCEPT;

LY_API ly_status ly_extract(
    ly_context* context,
    const char* url,
    size_t url_len,
    ly_result** out,
    ly_error** error
) LY_NOEXCEPT;

LY_API ly_status ly_version_info(ly_result** out, ly_error** error) LY_NOEXCEPT;

LY_API ly_status
ly_result_json(const ly_result* result, const char** data, size_t* len) LY_NOEXCEPT;
LY_API void ly_result_release(ly_result* result) LY_NOEXCEPT;

LY_API ly_status ly_error_code(const ly_error* error) LY_NOEXCEPT;
LY_API const char* ly_error_message(const ly_error* error) LY_NOEXCEPT;
LY_API void ly_error_release(ly_error* error) LY_NOEXCEPT;

#ifdef __cplusplus
}
#endif

#endif
