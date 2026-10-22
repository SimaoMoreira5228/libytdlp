#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <dirent.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

#include <libytdlp/libytdlp.h>

#include "fixture.h"

#ifdef _WIN32
static void sleep_between_polls(void) {
    Sleep(10);
}
#else
static void sleep_between_polls(void) {
    usleep(10000);
}
#endif

static void check_job_null_handling(void) {
    ly_context* context = NULL;
    ly_job* job = NULL;
    ly_result* events = NULL;
    ly_job_state state = LY_JOB_DONE;
    ly_error* error = (ly_error*)0x1;

    assert(ly_context_create(NULL, 0, &context, NULL) == LY_OK);

    assert(ly_download_start(NULL, "x", 1, NULL, 0, &job, NULL) == LY_E_INVALID_ARGUMENT);
    assert(ly_download_start(context, NULL, 0, NULL, 0, &job, NULL) == LY_E_INVALID_ARGUMENT);
    assert(ly_download_start(context, "", 0, NULL, 0, &job, NULL) == LY_E_INVALID_ARGUMENT);
    assert(ly_download_start(context, "x", 1, NULL, 0, NULL, NULL) == LY_E_INVALID_ARGUMENT);
    assert(ly_download_start(context, "x", 1, NULL, 0, NULL, &error) == LY_E_INVALID_ARGUMENT);
    assert(error != NULL);
    ly_error_release(error);
    assert(ly_download_start(context, "x", 1, NULL, 3, &job, NULL) == LY_E_INVALID_ARGUMENT);
    assert(ly_download_start(context, "x", 1, "{}", 4, NULL, &error) == LY_E_INVALID_ARGUMENT);
    assert(error != NULL);
    ly_error_release(error);

    assert(ly_error_details_json(NULL, NULL, NULL) == LY_E_INVALID_ARGUMENT);

    assert(ly_job_poll(NULL, &state, &events, NULL) == LY_E_INVALID_ARGUMENT);
    assert(ly_job_wait(NULL, &events, NULL) == LY_E_INVALID_ARGUMENT);
    assert(ly_job_wait(job, NULL, NULL) == LY_E_INVALID_ARGUMENT);
    assert(ly_job_cancel(NULL) == LY_E_INVALID_ARGUMENT);
    assert(ly_job_cancel(job) == LY_E_INVALID_ARGUMENT);

    ly_job_release(NULL);

    assert(ly_context_release(context) == LY_OK);
}

#ifdef _WIN32
static void make_tmpdir(char* path, size_t len) {
    assert(len > 32);

    char base[MAX_PATH];
    DWORD got = GetTempPathA(sizeof(base), base);
    assert(got != 0);
    assert(got < sizeof(base));

    static int counter = 0;
    counter++;

    int written =
        snprintf(path, len, "%sly_job_%lu_%d", base, (unsigned long)GetCurrentProcessId(), counter);
    assert(written > 0);
    assert((size_t)written < len);
    assert(CreateDirectoryA(path, NULL) != 0);

    for (size_t i = 0; path[i] != '\0'; i++) {
        if (path[i] == '\\') {
            path[i] = '/';
        }
    }
}

static void remove_tree(const char* dir) {
    char pattern[512];
    int written = snprintf(pattern, sizeof(pattern), "%s\\*", dir);
    assert(written > 0);
    assert((size_t)written < sizeof(pattern));

    WIN32_FIND_DATAA found = {0};
    HANDLE listing = FindFirstFileA(pattern, &found);
    assert(listing != INVALID_HANDLE_VALUE);

    for (;;) {
        if (strcmp(found.cFileName, ".") != 0 && strcmp(found.cFileName, "..") != 0) {
            char child[512];
            int child_written = snprintf(child, sizeof(child), "%s\\%s", dir, found.cFileName);
            assert(child_written > 0);
            assert((size_t)child_written < sizeof(child));
            assert(DeleteFileA(child) != 0);
        }

        if (FindNextFileA(listing, &found) == 0) {
            break;
        }
    }

    assert(GetLastError() == ERROR_NO_MORE_FILES);
    assert(FindClose(listing) != 0);
    assert(RemoveDirectoryA(dir) != 0);
}
#else
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
#endif

static void check_job_bogus_url(void) {
    ly_context* context = NULL;
    assert(ly_context_create(NULL, 0, &context, NULL) == LY_OK);

    static const char url[] = "ly-bogus://no-such-scheme-xyz";

    ly_job* job = NULL;
    ly_error* error = NULL;
    assert(ly_download_start(context, url, sizeof(url) - 1, NULL, 0, &job, &error) == LY_OK);
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
            sleep_between_polls();
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
    assert(ly_error_is_retryable(error) == false);
    printf("bogus download -> code %d: %s\n", ly_error_code(error), ly_error_message(error));

    const char* details = NULL;
    size_t details_len = 0;
    assert(ly_error_details_json(error, &details, &details_len) == LY_OK);
    assert(details != NULL);
    assert(details_len > 0);
    assert(details[0] == '{');
    assert(details[details_len - 1] == '}');
    assert(ly_error_details_json(error, NULL, &details_len) == LY_E_INVALID_ARGUMENT);
    assert(ly_error_details_json(error, &details, NULL) == LY_E_INVALID_ARGUMENT);
    assert(ly_error_details_json(NULL, &details, &details_len) == LY_E_INVALID_ARGUMENT);

    char want_code[32];
    snprintf(want_code, sizeof(want_code), "\"code\":%d", ly_error_code(error));
    assert(strstr(details, want_code) != NULL);
    assert(strstr(details, "\"kind\":\"extract\"") != NULL);
    assert(strstr(details, "\"class\":\"NoSupportingHandlers\"") != NULL);
    assert(strstr(details, "\"message\":\"NoSupportingHandlers") != NULL);
    printf("bogus download details -> %s\n", details);
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
    ly_process child = spawn_fixture("20000000", "200000", &port);

    char dir[256];
    make_tmpdir(dir, sizeof(dir));

    char options[512];
    snprintf(options, sizeof(options), "{\"outtmpl\": \"%s/big.bin\", \"proxy\": \"\"}", dir);

    ly_context* context = NULL;
    assert(ly_context_create(options, strlen(options), &context, NULL) == LY_OK);

    char url[128];
    snprintf(url, sizeof(url), "http://127.0.0.1:%d/big.bin", port);

    ly_job* job = NULL;
    ly_error* error = NULL;
    assert(ly_download_start(context, url, strlen(url), NULL, 0, &job, &error) == LY_OK);
    assert(job != NULL);

    ly_result* events = NULL;
    ly_job_state state = LY_JOB_RUNNING;

    for (int i = 0; i < 300 && state == LY_JOB_RUNNING; i++) {
        ly_result_release(events);
        events = NULL;
        assert(ly_job_poll(job, &state, &events, NULL) == LY_OK);
        assert(events != NULL);
        if (state == LY_JOB_RUNNING) {
            sleep_between_polls();
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
    assert(ly_error_is_retryable(error) == false);
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
    ly_process child = spawn_fixture("65536", "8000000", &port);

    char dir[256];
    make_tmpdir(dir, sizeof(dir));

    char target[300];
    snprintf(target, sizeof(target), "%s/out.bin", dir);

    char options[512];
    snprintf(options, sizeof(options), "{\"outtmpl\": \"%s\", \"proxy\": \"\"}", target);

    ly_context* context = NULL;
    assert(ly_context_create(options, strlen(options), &context, NULL) == LY_OK);

    char url[128];
    snprintf(url, sizeof(url), "http://127.0.0.1:%d/file.bin", port);

    ly_job* job = NULL;
    ly_error* error = NULL;
    assert(ly_download_start(context, url, strlen(url), NULL, 0, &job, &error) == LY_OK);
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
    ly_process child = spawn_fixture("65536", "8000000", &port);

    char dir[256];
    make_tmpdir(dir, sizeof(dir));

    char options[512];
    snprintf(options, sizeof(options), "{\"outtmpl\": \"%s/out.bin\", \"proxy\": \"\"}", dir);

    ly_context* context = NULL;
    assert(ly_context_create(options, strlen(options), &context, NULL) == LY_OK);

    char url[128];
    snprintf(url, sizeof(url), "http://127.0.0.1:%d/file.bin", port);

    ly_job* job = NULL;
    assert(ly_download_start(context, url, strlen(url), NULL, 0, &job, NULL) == LY_OK);
    assert(job != NULL);

    ly_job_release(job);

    assert(ly_context_release(context) == LY_OK);

    remove_tree(dir);

    stop_fixture(child);
}

#ifdef _WIN32
static int dir_is_empty(const char* dir) {
    char pattern[512];
    int written = snprintf(pattern, sizeof(pattern), "%s\\*", dir);
    assert(written > 0);
    assert((size_t)written < sizeof(pattern));

    WIN32_FIND_DATAA found = {0};
    HANDLE listing = FindFirstFileA(pattern, &found);
    assert(listing != INVALID_HANDLE_VALUE);

    int count = 0;

    for (;;) {
        if (strcmp(found.cFileName, ".") != 0 && strcmp(found.cFileName, "..") != 0) {
            count++;
        }

        if (FindNextFileA(listing, &found) == 0) {
            break;
        }
    }

    assert(GetLastError() == ERROR_NO_MORE_FILES);
    assert(FindClose(listing) != 0);
    return count == 0;
}
#else
static int dir_is_empty(const char* dir) {
    DIR* listing = opendir(dir);
    assert(listing != NULL);

    int count = 0;
    for (;;) {
        struct dirent* entry = readdir(listing);
        if (entry == NULL) {
            break;
        }

        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        count++;
    }

    assert(closedir(listing) == 0);
    return count == 0;
}
#endif

static void check_job_per_download_override(void) {
    int port = 0;
    ly_process child = spawn_fixture("65536", "8000000", &port);

    char dir_a[256];
    make_tmpdir(dir_a, sizeof(dir_a));

    char dir_b[256];
    make_tmpdir(dir_b, sizeof(dir_b));

    char context_options[512];
    snprintf(
        context_options,
        sizeof(context_options),
        "{\"outtmpl\": \"%s/ctx.bin\", \"proxy\": \"\"}",
        dir_a
    );

    char job_options[512];
    snprintf(job_options, sizeof(job_options), "{\"outtmpl\": \"%s/job.bin\"}", dir_b);

    ly_context* context = NULL;
    assert(ly_context_create(context_options, strlen(context_options), &context, NULL) == LY_OK);

    char url[128];
    snprintf(url, sizeof(url), "http://127.0.0.1:%d/file.bin", port);

    ly_job* job = NULL;
    ly_error* error = NULL;
    assert(
        ly_download_start(
            context,
            url,
            strlen(url),
            job_options,
            strlen(job_options),
            &job,
            &error
        ) == LY_OK
    );
    assert(job != NULL);

    ly_result* out = NULL;
    assert(ly_job_wait(job, &out, &error) == LY_OK);
    assert(out != NULL);
    ly_result_release(out);

    ly_job_release(job);
    assert(ly_context_release(context) == LY_OK);

    char target[300];
    snprintf(target, sizeof(target), "%s/job.bin", dir_b);

    FILE* probe = fopen(target, "rb");
    assert(probe != NULL);
    assert(fseek(probe, 0, SEEK_END) == 0);
    assert(ftell(probe) == 65536);
    assert(fclose(probe) == 0);

    assert(dir_is_empty(dir_a));

    remove_tree(dir_a);
    remove_tree(dir_b);

    stop_fixture(child);
}

static void check_job_download_restricted(void) {
    int port = 0;
    ly_process child = spawn_fixture("65536", "8000000", &port);

    char dir[256];
    make_tmpdir(dir, sizeof(dir));

    char target[300];
    snprintf(target, sizeof(target), "%s/ok.bin", dir);

    char context_options[512];
    snprintf(
        context_options,
        sizeof(context_options),
        "{\"outtmpl\": \"%s\", \"proxy\": \"\"}",
        target
    );

    char job_options[512];
    snprintf(job_options, sizeof(job_options), "{\"libytdlp\": {\"restrict_to\": \"%s\"}}", dir);

    ly_context* context = NULL;
    assert(ly_context_create(context_options, strlen(context_options), &context, NULL) == LY_OK);

    char url[128];
    snprintf(url, sizeof(url), "http://127.0.0.1:%d/file.bin", port);

    ly_job* job = NULL;
    ly_error* error = NULL;
    assert(
        ly_download_start(
            context,
            url,
            strlen(url),
            job_options,
            strlen(job_options),
            &job,
            &error
        ) == LY_OK
    );
    assert(job != NULL);

    ly_result* out = NULL;
    assert(ly_job_wait(job, &out, &error) == LY_OK);
    assert(out != NULL);
    ly_result_release(out);

    FILE* probe = fopen(target, "rb");
    assert(probe != NULL);
    assert(fseek(probe, 0, SEEK_END) == 0);
    assert(ftell(probe) == 65536);
    assert(fclose(probe) == 0);

    ly_job_release(job);
    assert(ly_context_release(context) == LY_OK);

    remove_tree(dir);

    stop_fixture(child);
}

static void check_job_download_restricted_reject(void) {
    int port = 0;
    ly_process child = spawn_fixture("65536", "8000000", &port);

    char dir_a[256];
    make_tmpdir(dir_a, sizeof(dir_a));

    char dir_b[256];
    make_tmpdir(dir_b, sizeof(dir_b));

    char target[300];
    snprintf(target, sizeof(target), "%s/ok.bin", dir_a);

    char context_options[512];
    snprintf(context_options, sizeof(context_options), "{\"outtmpl\": \"%s\"}", target);

    char job_options[512];
    snprintf(job_options, sizeof(job_options), "{\"libytdlp\": {\"restrict_to\": \"%s\"}}", dir_b);

    ly_context* context = NULL;
    assert(ly_context_create(context_options, strlen(context_options), &context, NULL) == LY_OK);

    char url[128];
    snprintf(url, sizeof(url), "http://127.0.0.1:%d/file.bin", port);

    ly_job* job = NULL;
    ly_error* error = NULL;
    assert(
        ly_download_start(
            context,
            url,
            strlen(url),
            job_options,
            strlen(job_options),
            &job,
            &error
        ) == LY_OK
    );
    assert(job != NULL);

    ly_result* out = NULL;
    ly_status status = ly_job_wait(job, &out, &error);
    assert(status == LY_E_INVALID_ARGUMENT);
    assert(out == NULL);
    assert(error != NULL);
    assert(ly_error_code(error) == LY_E_INVALID_ARGUMENT);

    const char* details = NULL;
    size_t details_len = 0;
    assert(ly_error_details_json(error, &details, &details_len) == LY_OK);
    assert(details != NULL);
    assert(details_len > 0);
    assert(strstr(details, "\"kind\":\"invalid_argument\"") != NULL);
    ly_error_release(error);

#ifdef _WIN32
    assert(_access(target, 0) != 0);
#else
    assert(access(target, F_OK) != 0);
#endif
    assert(dir_is_empty(dir_b));

    ly_job_release(job);
    assert(ly_context_release(context) == LY_OK);

    remove_tree(dir_a);
    remove_tree(dir_b);

    stop_fixture(child);
}

#ifdef _WIN32
static int closed_loopback_port(void) {
    WSADATA data = {0};
    assert(WSAStartup(MAKEWORD(2, 2), &data) == 0);

    SOCKET fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    assert(fd != INVALID_SOCKET);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(0);
    assert(bind(fd, (struct sockaddr*)&addr, sizeof(addr)) == 0);

    int len = sizeof(addr);
    assert(getsockname(fd, (struct sockaddr*)&addr, &len) == 0);
    int port = ntohs(addr.sin_port);
    assert(port > 0);
    assert(closesocket(fd) == 0);
    assert(WSACleanup() == 0);
    return port;
}
#else
static int closed_loopback_port(void) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    assert(fd >= 0);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(0);
    assert(bind(fd, (struct sockaddr*)&addr, sizeof(addr)) == 0);

    socklen_t len = sizeof(addr);
    assert(getsockname(fd, (struct sockaddr*)&addr, &len) == 0);
    int port = ntohs(addr.sin_port);
    assert(port > 0);
    assert(close(fd) == 0);
    return port;
}
#endif

static void check_job_closed_port_network(void) {
    int port = closed_loopback_port();

    ly_context* context = NULL;
    assert(ly_context_create(NULL, 0, &context, NULL) == LY_OK);

    char url[64];
    snprintf(url, sizeof(url), "http://127.0.0.1:%d/file.bin", port);

    ly_job* job = NULL;
    ly_error* error = NULL;
    assert(ly_download_start(context, url, strlen(url), NULL, 0, &job, &error) == LY_OK);
    assert(job != NULL);

    ly_result* out = NULL;
    ly_status status = ly_job_wait(job, &out, &error);
    assert(status == LY_E_NETWORK);
    assert(out == NULL);
    assert(error != NULL);
    assert(ly_error_code(error) == LY_E_NETWORK);
    assert(ly_error_is_retryable(error) == true);

    const char* details = NULL;
    size_t details_len = 0;
    assert(ly_error_details_json(error, &details, &details_len) == LY_OK);
    assert(details != NULL);
    assert(details_len > 0);

    char want_code[32];
    snprintf(want_code, sizeof(want_code), "\"code\":%d", LY_E_NETWORK);
    assert(strstr(details, want_code) != NULL);
    assert(strstr(details, "\"kind\":\"network\"") != NULL);
    assert(strstr(details, "\"class\":\"DownloadError\"") != NULL);
    printf("closed port -> code %d: %s\n", ly_error_code(error), ly_error_message(error));
    printf("closed port details -> %s\n", details);
    ly_error_release(error);
    error = NULL;

    status = ly_job_wait(job, &out, &error);
    assert(status == LY_E_NETWORK);
    assert(out == NULL);
    ly_error_release(error);

    ly_result* events = NULL;
    ly_job_state state = LY_JOB_RUNNING;
    assert(ly_job_poll(job, &state, &events, NULL) == LY_OK);
    assert(state == LY_JOB_FAILED);
    ly_result_release(events);

    ly_job_release(job);
    assert(ly_context_release(context) == LY_OK);
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    assert(ly_abi_version() == LIBYTDLP_ABI_VERSION);
    printf("abi %u\n", ly_abi_version());

    check_job_null_handling();
    printf("job null handling ok\n");

    check_job_bogus_url();
    printf("job bogus url ok\n");

    check_job_cancel();
    printf("job cancel ok\n");

    check_job_download_success();
    printf("job download success ok\n");

    check_job_per_download_override();
    printf("job per-download override ok\n");

    check_job_download_restricted();
    printf("job download restricted ok\n");

    check_job_download_restricted_reject();
    printf("job download restricted reject ok\n");

    check_job_closed_port_network();
    printf("job closed port network ok\n");

    check_job_release_joins();
    printf("job release joins ok\n");

    printf("jobs ok\n");
    return 0;
}
