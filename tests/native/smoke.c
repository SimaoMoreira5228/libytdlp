#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Python.h>

#ifdef _WIN32
#include <process.h>
#include <windows.h>
#else
#include <pthread.h>
#endif

#include <libytdlp/libytdlp.h>

#include "fixture.h"

static void check_host_owned_interpreter(void) {
    Py_Initialize();

    PyThreadState* host_state = PyEval_SaveThread();

    ly_context* context = NULL;
    assert(ly_context_create(NULL, 0, &context, NULL) == LY_OK);

    static const char url[] = "ly-bogus://no-such-scheme-xyz";

    ly_result* result = NULL;
    ly_error* error = NULL;
    assert(ly_extract(context, url, sizeof(url) - 1, NULL, 0, &result, &error) != LY_OK);
    assert(result == NULL);
    ly_error_release(error);
    error = NULL;

    ly_result* info = NULL;
    ly_status info_status = ly_version_info(&info, &error);

    if (info_status != LY_OK) {
        fprintf(
            stderr,
            "host-owned version info failed: %s\n",
            error != NULL ? ly_error_message(error) : "no detail"
        );
    }

    assert(info_status == LY_OK);

    const char* data = NULL;
    size_t len = 0;
    assert(ly_result_json(info, &data, &len) == LY_OK);
    assert(data != NULL);
    assert(strstr(data, "\"host_owned\":true") != NULL);
    ly_result_release(info);

    assert(ly_context_release(context) == LY_OK);

    PyEval_RestoreThread(host_state);
    assert(PyRun_SimpleString("pass") == 0);

    (void)PyEval_SaveThread();
}

static void check_null_handling(void) {
    ly_context* context = NULL;
    ly_result* result = NULL;
    ly_error* error = (ly_error*)0x1;

    assert(ly_context_create(NULL, 0, NULL, NULL) == LY_E_INVALID_ARGUMENT);
    assert(ly_context_create(NULL, 0, NULL, &error) == LY_E_INVALID_ARGUMENT);
    assert(error != NULL);
    assert(ly_error_code(error) == LY_E_INVALID_ARGUMENT);
    assert(ly_error_message(error) != NULL);
    ly_error_release(error);

    assert(ly_context_release(NULL) == LY_OK);
    assert(ly_extract(NULL, NULL, 0, NULL, 0, NULL, NULL) == LY_E_INVALID_ARGUMENT);
    assert(ly_extract(NULL, "x", 1, NULL, 0, &result, NULL) == LY_E_INVALID_ARGUMENT);
    assert(ly_result_json(NULL, NULL, NULL) == LY_E_INVALID_ARGUMENT);
    assert(ly_error_code(NULL) == LY_E_INVALID_ARGUMENT);
    assert(ly_error_message(NULL) == NULL);
    assert(ly_version_info(NULL, NULL) == LY_E_INVALID_ARGUMENT);

    ly_result_release(NULL);
    ly_error_release(NULL);

    assert(ly_error_is_retryable(NULL) == false);

    assert(ly_context_create(NULL, 0, &context, NULL) == LY_OK);
    assert(context != NULL);
    assert(ly_extract(context, NULL, 0, NULL, 0, &result, NULL) == LY_E_INVALID_ARGUMENT);
    assert(ly_extract(context, "", 0, NULL, 0, &result, NULL) == LY_E_INVALID_ARGUMENT);
    assert(ly_extract(context, "x", 1, NULL, 3, &result, NULL) == LY_E_INVALID_ARGUMENT);
    assert(ly_context_release(context) == LY_OK);
}

static void check_create_release_loop(void) {
    for (int i = 0; i < 200; i++) {
        ly_context* context = NULL;
        assert(ly_context_create(NULL, 0, &context, NULL) == LY_OK);
        assert(context != NULL);
        assert(ly_context_release(context) == LY_OK);
    }
}

static void check_bad_options(void) {
    static const char bad[] = "{not json";
    static const char bad_url[] = "ly-bogus://example";

    ly_context* context = NULL;
    assert(ly_context_create(bad, sizeof(bad) - 1, &context, NULL) == LY_OK);

    ly_result* result = NULL;
    ly_error* error = NULL;
    ly_status status = ly_extract(context, bad_url, sizeof(bad_url) - 1, NULL, 0, &result, &error);
    assert(status == LY_E_INVALID_ARGUMENT);
    assert(result == NULL);
    assert(error != NULL);
    assert(ly_error_code(error) == LY_E_INVALID_ARGUMENT);
    assert(ly_error_message(error) != NULL);
    assert(strlen(ly_error_message(error)) > 0);
    assert(ly_error_is_retryable(error) == false);
    ly_error_release(error);
    error = NULL;

    static const char bad_call[] = "[1, 2]";
    status = ly_extract(
        context,
        bad_url,
        sizeof(bad_url) - 1,
        bad_call,
        sizeof(bad_call) - 1,
        &result,
        &error
    );
    assert(status == LY_E_INVALID_ARGUMENT);
    assert(result == NULL);
    ly_error_release(error);

    assert(ly_context_release(context) == LY_OK);
}

static void check_unsupported_url(void) {
    ly_context* context = NULL;
    assert(ly_context_create(NULL, 0, &context, NULL) == LY_OK);

    static const char url[] = "ly-bogus://no-such-scheme-xyz";

    ly_result* result = NULL;
    ly_error* error = NULL;
    ly_status status = ly_extract(context, url, sizeof(url) - 1, NULL, 0, &result, &error);
    assert(status != LY_OK);
    assert(result == NULL);
    assert(error != NULL);
    assert(ly_error_message(error) != NULL);
    assert(ly_error_is_retryable(error) == false);
    printf("unsupported url -> code %d: %s\n", ly_error_code(error), ly_error_message(error));
    ly_error_release(error);

    assert(ly_context_release(context) == LY_OK);
}

#ifdef _WIN32
static unsigned __stdcall extract_bad_url(void* arg) {
#else
static void* extract_bad_url(void* arg) {
#endif
    static const char url[] = "ly-bogus://no-such-scheme-xyz";

    ly_result* result = NULL;
    ly_error* error = NULL;
    ly_status status = ly_extract(arg, url, sizeof(url) - 1, NULL, 0, &result, &error);

    assert(status != LY_OK);
    assert(result == NULL);
    ly_error_release(error);
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

static void check_concurrent_extract(void) {
    ly_context* context = NULL;
    assert(ly_context_create(NULL, 0, &context, NULL) == LY_OK);

#ifdef _WIN32
    HANDLE workers[8];

    for (int i = 0; i < 8; i++) {
        workers[i] = (HANDLE)_beginthreadex(NULL, 0, extract_bad_url, context, 0, NULL);
        assert(workers[i] != NULL);
    }

    for (int i = 0; i < 8; i++) {
        assert(WaitForSingleObject(workers[i], INFINITE) == WAIT_OBJECT_0);
        assert(CloseHandle(workers[i]) != 0);
    }
#else
    pthread_t workers[8];

    for (int i = 0; i < 8; i++) {
        assert(pthread_create(&workers[i], NULL, extract_bad_url, context) == 0);
    }

    for (int i = 0; i < 8; i++) {
        assert(pthread_join(workers[i], NULL) == 0);
    }
#endif

    assert(ly_context_release(context) == LY_OK);
}

static void check_version_info(void) {
    ly_result* result = NULL;
    ly_error* error = NULL;
    assert(ly_version_info(&result, &error) == LY_OK);
    assert(result != NULL);

    const char* data = NULL;
    size_t len = 0;
    assert(ly_result_json(result, &data, &len) == LY_OK);
    assert(data != NULL);
    assert(len > 50);
    assert(strstr(data, "deployment_profile") != NULL);
#ifdef LIBYTDLP_DEPLOYMENT_PROFILE
    assert(strstr(data, LIBYTDLP_DEPLOYMENT_PROFILE) != NULL);
#else
    assert(strstr(data, "FULL_BUNDLED") != NULL);
#endif
    printf("version info ok: %zu bytes of json\n", len);
    ly_result_release(result);
}

static void check_network_url(void) {
    const char* url = getenv("LIBYTDLP_TEST_URL");
    if (url == NULL || url[0] == '\0') {
        printf("no LIBYTDLP_TEST_URL, skipping network check\n");
        return;
    }

    ly_context* context = NULL;
    assert(ly_context_create(NULL, 0, &context, NULL) == LY_OK);

    ly_result* result = NULL;
    ly_error* error = NULL;
    ly_status status = ly_extract(context, url, strlen(url), NULL, 0, &result, &error);
    if (status != LY_OK) {
        printf(
            "network check failed (flaky is fine): %s\n",
            error != NULL ? ly_error_message(error) : "no detail"
        );
        ly_error_release(error);
        assert(ly_context_release(context) == LY_OK);
        return;
    }

    const char* data = NULL;
    size_t len = 0;
    assert(ly_result_json(result, &data, &len) == LY_OK);
    assert(data != NULL);
    assert(len > 100);
    printf("network check ok: %zu bytes of json\n", len);
    ly_result_release(result);

    assert(ly_context_release(context) == LY_OK);
}

static void check_extract_per_call_override(void) {
    int port = 0;
    ly_process child = spawn_fixture("65536", "8000000", &port);

    char url[128];
    snprintf(url, sizeof(url), "http://127.0.0.1:%d/file.bin", port);

    static const char context_options[] = "{\"skip_download\": true, \"proxy\": \"\"}";

    ly_context* context = NULL;
    assert(
        ly_context_create(context_options, sizeof(context_options) - 1, &context, NULL) == LY_OK
    );

    ly_result* result = NULL;
    ly_error* error = NULL;

    static const char empty_call[] = "{}";
    assert(
        ly_extract(
            context,
            url,
            strlen(url),
            empty_call,
            sizeof(empty_call) - 1,
            &result,
            &error
        ) == LY_OK
    );

    const char* data = NULL;
    size_t len = 0;
    assert(ly_result_json(result, &data, &len) == LY_OK);
    assert(data != NULL);
    assert(strstr(data, "\"id\":\"file\"") != NULL);
    ly_result_release(result);
    result = NULL;

    static const char force_call[] = "{\"extractor_args\": {\"generic\": {\"force\": [true]}}}";
    assert(
        ly_extract(
            context,
            url,
            strlen(url),
            force_call,
            sizeof(force_call) - 1,
            &result,
            &error
        ) == LY_OK
    );
    assert(ly_result_json(result, &data, &len) == LY_OK);
    assert(data != NULL);
    assert(strstr(data, "\"id\":\"file\"") != NULL);
    ly_result_release(result);
    result = NULL;

    static const char bad_call[] = "[1, 2]";
    assert(
        ly_extract(context, url, strlen(url), bad_call, sizeof(bad_call) - 1, &result, &error) ==
        LY_E_INVALID_ARGUMENT
    );
    assert(result == NULL);
    ly_error_release(error);
    error = NULL;

    assert(ly_context_release(context) == LY_OK);
    context = NULL;

    static const char poisoned[] = "{\"proxy\": \"http://127.0.0.1:1\"}";
    assert(ly_context_create(poisoned, sizeof(poisoned) - 1, &context, NULL) == LY_OK);

    assert(ly_extract(context, url, strlen(url), NULL, 0, &result, &error) != LY_OK);
    assert(result == NULL);
    ly_error_release(error);
    error = NULL;

    static const char clear_proxy[] = "{\"proxy\": null}";
    assert(
        ly_extract(
            context,
            url,
            strlen(url),
            clear_proxy,
            sizeof(clear_proxy) - 1,
            &result,
            &error
        ) == LY_OK
    );
    assert(ly_result_json(result, &data, &len) == LY_OK);
    assert(data != NULL);
    assert(strstr(data, "\"id\":\"file\"") != NULL);
    ly_result_release(result);

    assert(ly_context_release(context) == LY_OK);

    stop_fixture(child);
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    assert(ly_abi_version() == LIBYTDLP_ABI_VERSION);
    printf("abi %u\n", ly_abi_version());

    check_host_owned_interpreter();
    printf("host-owned interpreter ok\n");

    check_null_handling();
    printf("null handling ok\n");

    check_create_release_loop();
    printf("create/release loop ok\n");

    check_bad_options();
    printf("bad options ok\n");

    check_unsupported_url();
    printf("unsupported url ok\n");

    check_concurrent_extract();
    printf("concurrent extract ok\n");

    check_version_info();
    printf("version info ok\n");

    check_network_url();

    check_extract_per_call_override();
    printf("extract per-call override ok\n");

    printf("smoke ok\n");
    return 0;
}
