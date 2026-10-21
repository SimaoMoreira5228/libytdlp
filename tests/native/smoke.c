#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pthread.h>

#include <libytdlp/libytdlp.h>

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
    assert(ly_extract(NULL, NULL, 0, NULL, NULL) == LY_E_INVALID_ARGUMENT);
    assert(ly_extract(NULL, "x", 1, &result, NULL) == LY_E_INVALID_ARGUMENT);
    assert(ly_result_json(NULL, NULL, NULL) == LY_E_INVALID_ARGUMENT);
    assert(ly_error_code(NULL) == LY_E_INVALID_ARGUMENT);
    assert(ly_error_message(NULL) == NULL);

    ly_result_release(NULL);
    ly_error_release(NULL);

    assert(ly_context_create(NULL, 0, &context, NULL) == LY_OK);
    assert(context != NULL);
    assert(ly_extract(context, NULL, 0, &result, NULL) == LY_E_INVALID_ARGUMENT);
    assert(ly_extract(context, "", 0, &result, NULL) == LY_E_INVALID_ARGUMENT);
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
    ly_status status = ly_extract(context, bad_url, sizeof(bad_url) - 1, &result, &error);
    assert(status == LY_E_INVALID_ARGUMENT);
    assert(result == NULL);
    assert(error != NULL);
    assert(ly_error_code(error) == LY_E_INVALID_ARGUMENT);
    assert(ly_error_message(error) != NULL);
    assert(strlen(ly_error_message(error)) > 0);
    ly_error_release(error);

    assert(ly_context_release(context) == LY_OK);
}

static void check_unsupported_url(void) {
    ly_context* context = NULL;
    assert(ly_context_create(NULL, 0, &context, NULL) == LY_OK);

    static const char url[] = "ly-bogus://no-such-scheme-xyz";

    ly_result* result = NULL;
    ly_error* error = NULL;
    ly_status status = ly_extract(context, url, sizeof(url) - 1, &result, &error);
    assert(status != LY_OK);
    assert(result == NULL);
    assert(error != NULL);
    assert(ly_error_message(error) != NULL);
    printf("unsupported url -> code %d: %s\n", ly_error_code(error), ly_error_message(error));
    ly_error_release(error);

    assert(ly_context_release(context) == LY_OK);
}

static void* extract_bad_url(void* arg) {
    static const char url[] = "ly-bogus://no-such-scheme-xyz";

    ly_result* result = NULL;
    ly_error* error = NULL;
    ly_status status = ly_extract(arg, url, sizeof(url) - 1, &result, &error);

    assert(status != LY_OK);
    assert(result == NULL);
    ly_error_release(error);
    return NULL;
}

static void check_concurrent_extract(void) {
    ly_context* context = NULL;
    assert(ly_context_create(NULL, 0, &context, NULL) == LY_OK);

    pthread_t workers[8];
    for (int i = 0; i < 8; i++) {
        assert(pthread_create(&workers[i], NULL, extract_bad_url, context) == 0);
    }

    for (int i = 0; i < 8; i++) {
        assert(pthread_join(workers[i], NULL) == 0);
    }

    assert(ly_context_release(context) == LY_OK);
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
    ly_status status = ly_extract(context, url, strlen(url), &result, &error);
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

int main(void) {
    assert(ly_abi_version() == LIBYTDLP_ABI_VERSION);
    printf("abi %u\n", ly_abi_version());

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

    check_network_url();

    printf("smoke ok\n");
    return 0;
}
