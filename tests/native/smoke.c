#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <dirent.h>

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
    assert(ly_version_info(NULL, NULL) == LY_E_INVALID_ARGUMENT);

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

static void check_job_null_handling(void) {
    ly_context* context = NULL;
    ly_job* job = NULL;
    ly_result* events = NULL;
    ly_job_state state = LY_JOB_DONE;
    ly_error* error = (ly_error*)0x1;

    assert(ly_context_create(NULL, 0, &context, NULL) == LY_OK);

    assert(ly_download_start(NULL, "x", 1, &job, NULL) == LY_E_INVALID_ARGUMENT);
    assert(ly_download_start(context, NULL, 0, &job, NULL) == LY_E_INVALID_ARGUMENT);
    assert(ly_download_start(context, "", 0, &job, NULL) == LY_E_INVALID_ARGUMENT);
    assert(ly_download_start(context, "x", 1, NULL, NULL) == LY_E_INVALID_ARGUMENT);
    assert(ly_download_start(context, "x", 1, NULL, &error) == LY_E_INVALID_ARGUMENT);
    assert(error != NULL);
    ly_error_release(error);

    assert(ly_job_poll(NULL, &state, &events, NULL) == LY_E_INVALID_ARGUMENT);
    assert(ly_job_wait(NULL, &events, NULL) == LY_E_INVALID_ARGUMENT);
    assert(ly_job_wait(job, NULL, NULL) == LY_E_INVALID_ARGUMENT);
    assert(ly_job_cancel(NULL) == LY_E_INVALID_ARGUMENT);
    assert(ly_job_cancel(job) == LY_E_INVALID_ARGUMENT);

    ly_job_release(NULL);

    assert(ly_context_release(context) == LY_OK);
}

static pid_t spawn_fixture(const char* bytes, const char* rate, int* port_out) {
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
            bytes,
            rate,
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

static void make_tmpdir(char* path, size_t len) {
    assert(len > 32);
    snprintf(path, len, "/tmp/ly_job_XXXXXX");
    assert(mkdtemp(path) != NULL);
}

static void remove_tree(const char* dir) {
    DIR* listing = opendir(dir);
    assert(listing != NULL);

    for (;;) {
        struct dirent* entry = readdir(listing);
        if (entry == NULL) {
            break;
        }

        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        char child[512];
        snprintf(child, sizeof(child), "%s/%s", dir, entry->d_name);
        assert(unlink(child) == 0);
    }

    assert(closedir(listing) == 0);
    assert(rmdir(dir) == 0);
}

static void check_job_bogus_url(void) {
    ly_context* context = NULL;
    assert(ly_context_create(NULL, 0, &context, NULL) == LY_OK);

    static const char url[] = "ly-bogus://no-such-scheme-xyz";

    ly_job* job = NULL;
    ly_error* error = NULL;
    assert(ly_download_start(context, url, sizeof(url) - 1, &job, &error) == LY_OK);
    assert(job != NULL);

    ly_result* events = NULL;
    ly_job_state state = LY_JOB_RUNNING;
    ly_status status = LY_OK;

    for (int i = 0; i < 600 && state == LY_JOB_RUNNING; i++) {
        ly_result_release(events);
        events = NULL;
        status = ly_job_poll(job, &state, &events, NULL);
        assert(status == LY_OK);
        assert(events != NULL);
        if (state == LY_JOB_RUNNING) {
            usleep(10000);
        }
    }
    assert(state == LY_JOB_FAILED);
    ly_result_release(events);

    ly_result* out = NULL;
    status = ly_job_wait(job, &out, &error);
    assert(status == LY_E_EXTRACT);
    assert(out == NULL);
    assert(error != NULL);
    assert(ly_error_code(error) == LY_E_EXTRACT);
    printf("bogus download -> code %d: %s\n", ly_error_code(error), ly_error_message(error));
    ly_error_release(error);
    error = NULL;

    status = ly_job_wait(job, &out, &error);
    assert(status == LY_E_EXTRACT);
    assert(out == NULL);
    ly_error_release(error);

    status = ly_job_poll(job, &state, &events, NULL);
    assert(status == LY_OK);
    assert(state == LY_JOB_FAILED);
    const char* data = NULL;
    size_t len = 0;
    assert(ly_result_json(events, &data, &len) == LY_OK);
    assert(len == 2 && memcmp(data, "[]", 2) == 0);
    ly_result_release(events);

    assert(ly_job_cancel(job) == LY_OK);

    ly_job_release(job);
    assert(ly_context_release(context) == LY_OK);
}

static void check_job_cancel(void) {
    int port = 0;
    pid_t child = spawn_fixture("20000000", "200000", &port);

    char dir[256];
    make_tmpdir(dir, sizeof(dir));

    char options[512];
    snprintf(options, sizeof(options), "{\"outtmpl\": \"%s/big.bin\"}", dir);

    ly_context* context = NULL;
    assert(ly_context_create(options, strlen(options), &context, NULL) == LY_OK);

    char url[128];
    snprintf(url, sizeof(url), "http://127.0.0.1:%d/big.bin", port);

    ly_job* job = NULL;
    ly_error* error = NULL;
    assert(ly_download_start(context, url, strlen(url), &job, &error) == LY_OK);
    assert(job != NULL);

    ly_result* events = NULL;
    ly_job_state state = LY_JOB_RUNNING;

    for (int i = 0; i < 300 && state == LY_JOB_RUNNING; i++) {
        ly_result_release(events);
        events = NULL;
        assert(ly_job_poll(job, &state, &events, NULL) == LY_OK);
        assert(events != NULL);
        if (state == LY_JOB_RUNNING) {
            usleep(10000);
        }
    }

    const char* data = NULL;
    size_t len = 0;
    if (state == LY_JOB_RUNNING) {
        assert(ly_result_json(events, &data, &len) == LY_OK);
        printf("cancel test saw RUNNING, %zu bytes of events\n", len);
    }
    ly_result_release(events);
    events = NULL;

    assert(ly_job_cancel(job) == LY_OK);
    assert(ly_job_cancel(job) == LY_OK);

    ly_result* out = NULL;
    ly_status status = ly_job_wait(job, &out, &error);
    assert(status == LY_E_CANCELLED);
    assert(out == NULL);
    assert(error != NULL);
    assert(ly_error_code(error) == LY_E_CANCELLED);
    printf("cancelled download -> code %d: %s\n", ly_error_code(error), ly_error_message(error));
    ly_error_release(error);
    error = NULL;

    status = ly_job_wait(job, &out, &error);
    assert(status == LY_E_CANCELLED);
    assert(out == NULL);
    ly_error_release(error);

    state = LY_JOB_RUNNING;
    assert(ly_job_poll(job, &state, &events, NULL) == LY_OK);
    assert(state == LY_JOB_CANCELLED);
    ly_result_release(events);

    assert(ly_job_cancel(job) == LY_OK);

    ly_job_release(job);
    assert(ly_context_release(context) == LY_OK);

    remove_tree(dir);

    stop_fixture(child);
}

static void check_job_download_success(void) {
    int port = 0;
    pid_t child = spawn_fixture("65536", "8000000", &port);

    char dir[256];
    make_tmpdir(dir, sizeof(dir));

    char target[300];
    snprintf(target, sizeof(target), "%s/out.bin", dir);

    char options[512];
    snprintf(options, sizeof(options), "{\"outtmpl\": \"%s\"}", target);

    ly_context* context = NULL;
    assert(ly_context_create(options, strlen(options), &context, NULL) == LY_OK);

    char url[128];
    snprintf(url, sizeof(url), "http://127.0.0.1:%d/file.bin", port);

    ly_job* job = NULL;
    ly_error* error = NULL;
    assert(ly_download_start(context, url, strlen(url), &job, &error) == LY_OK);
    assert(job != NULL);

    ly_result* out = NULL;
    assert(ly_job_wait(job, &out, &error) == LY_OK);
    assert(out != NULL);

    const char* data = NULL;
    size_t len = 0;
    assert(ly_result_json(out, &data, &len) == LY_OK);
    assert(data != NULL);
    assert(len > 100);
    assert(strstr(data, "extractor") != NULL);

    ly_job_state state = LY_JOB_RUNNING;
    ly_result* events = NULL;
    assert(ly_job_poll(job, &state, &events, NULL) == LY_OK);
    assert(state == LY_JOB_DONE);
    ly_result_release(events);

    printf("fast download ok: %zu bytes of json\n", len);
    ly_result_release(out);

    FILE* probe = fopen(target, "rb");
    assert(probe != NULL);
    assert(fseek(probe, 0, SEEK_END) == 0);
    assert(ftell(probe) == 65536);
    fclose(probe);

    ly_job_release(job);
    assert(ly_context_release(context) == LY_OK);

    remove_tree(dir);

    stop_fixture(child);
}

static void check_job_release_joins(void) {
    int port = 0;
    pid_t child = spawn_fixture("65536", "8000000", &port);

    char dir[256];
    make_tmpdir(dir, sizeof(dir));

    char options[512];
    snprintf(options, sizeof(options), "{\"outtmpl\": \"%s/out.bin\"}", dir);

    ly_context* context = NULL;
    assert(ly_context_create(options, strlen(options), &context, NULL) == LY_OK);

    char url[128];
    snprintf(url, sizeof(url), "http://127.0.0.1:%d/file.bin", port);

    ly_job* job = NULL;
    assert(ly_download_start(context, url, strlen(url), &job, NULL) == LY_OK);
    assert(job != NULL);

    ly_job_release(job);

    assert(ly_context_release(context) == LY_OK);

    remove_tree(dir);

    stop_fixture(child);
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

    check_version_info();
    printf("version info ok\n");

    check_network_url();

    check_job_null_handling();
    printf("job null handling ok\n");

    check_job_bogus_url();
    printf("job bogus url ok\n");

    check_job_cancel();
    printf("job cancel ok\n");

    check_job_download_success();
    printf("job download success ok\n");

    check_job_release_joins();
    printf("job release joins ok\n");

    printf("smoke ok\n");
    return 0;
}
