#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <pthread.h>

#include <Python.h>

#include <libytdlp/libytdlp.h>

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
    assert(ly_version_info(&info, &error) == LY_OK);

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
    printf("unsupported url -> code %d: %s\n", ly_error_code(error), ly_error_message(error));
    ly_error_release(error);

    assert(ly_context_release(context) == LY_OK);
}

static void* extract_bad_url(void* arg) {
    static const char url[] = "ly-bogus://no-such-scheme-xyz";

    ly_result* result = NULL;
    ly_error* error = NULL;
    ly_status status = ly_extract(arg, url, sizeof(url) - 1, NULL, 0, &result, &error);

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
    assert(strstr(data, "FULL_BUNDLED") != NULL);
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

static pid_t spawn_fixture(int* port_out) {
    int fds[2];
    assert(pipe(fds) == 0);

    pid_t child = fork();
    assert(child >= 0);

    if (child == 0) {
        while (dup2(fds[1], STDOUT_FILENO) < 0 && errno == EINTR) {
        }
        close(fds[0]);
        close(fds[1]);
        execl(
            LIBYTDLP_TEST_PYTHON,
            LIBYTDLP_TEST_PYTHON,
            LIBYTDLP_SLOW_SERVER,
            "0",
            "65536",
            "8000000",
            (char*)NULL
        );
        _exit(127);
    }

    close(fds[1]);

    char line[64];
    size_t used = 0;
    for (;;) {
        assert(used + 1 < sizeof(line));
        ssize_t got = read(fds[0], line + used, 1);
        assert(got == 1);
        if (line[used] == '\n') {
            line[used] = '\0';
            break;
        }
        used++;
    }
    close(fds[0]);

    int port = 0;
    if (used > 5 && memcmp(line, "PORT ", 5) == 0) {
        char* end = NULL;
        long parsed = strtol(line + 5, &end, 10);
        if (end != line + 5 && (*end == '\n' || *end == '\0') && parsed > 0 && parsed < 65536) {
            port = (int)parsed;
        }
    }
    assert(port > 0);
    *port_out = port;
    return child;
}

static void stop_fixture(pid_t child) {
    kill(child, SIGKILL);

    int status = 0;
    while (waitpid(child, &status, 0) < 0 && errno == EINTR) {
    }
}

static char* save_env_value(const char* name) {
    const char* value = getenv(name);

    if (value == NULL) {
        return NULL;
    }

    size_t len = strlen(value) + 1;
    char* copy = malloc(len);
    assert(copy != NULL);
    memcpy(copy, value, len);
    return copy;
}

static void check_extract_per_call_override(void) {
    static const char* scrub[] = {
        "http_proxy",
        "https_proxy",
        "all_proxy",
        "HTTP_PROXY",
        "HTTPS_PROXY",
        "ALL_PROXY",
    };
    char* saved[6];

    for (size_t i = 0; i < 6; i++) {
        saved[i] = save_env_value(scrub[i]);
        unsetenv(scrub[i]);
    }

    char* saved_lower = save_env_value("no_proxy");
    char* saved_upper = save_env_value("NO_PROXY");
    setenv("no_proxy", "*", 1);
    setenv("NO_PROXY", "*", 1);

    int port = 0;
    pid_t child = spawn_fixture(&port);

    char url[128];
    snprintf(url, sizeof(url), "http://127.0.0.1:%d/file.bin", port);

    static const char context_options[] = "{\"skip_download\": true}";

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

    for (size_t i = 0; i < 6; i++) {
        if (saved[i] == NULL) {
            unsetenv(scrub[i]);
        } else {
            setenv(scrub[i], saved[i], 1);
            free(saved[i]);
        }
    }

    if (saved_lower == NULL) {
        unsetenv("no_proxy");
    } else {
        setenv("no_proxy", saved_lower, 1);
        free(saved_lower);
    }

    if (saved_upper == NULL) {
        unsetenv("NO_PROXY");
    } else {
        setenv("NO_PROXY", saved_upper, 1);
        free(saved_upper);
    }
}

int main(void) {
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
