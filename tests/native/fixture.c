#include "fixture.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#else
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#ifdef _WIN32
ly_process spawn_fixture(const char* bytes, const char* rate, int* port_out) {
    HANDLE read_pipe = NULL;
    HANDLE write_pipe = NULL;

    SECURITY_ATTRIBUTES security = {0};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;

    assert(CreatePipe(&read_pipe, &write_pipe, &security, 0) != 0);
    assert(SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0) != 0);

    char command[1024];
    int written = snprintf(
        command,
        sizeof(command),
        "\"%s\" \"%s\" 0 %s %s",
        LIBYTDLP_TEST_PYTHON,
        LIBYTDLP_SLOW_SERVER,
        bytes,
        rate
    );
    assert(written > 0);
    assert((size_t)written < sizeof(command));

    STARTUPINFOA startup = {0};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = write_pipe;
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);

    PROCESS_INFORMATION info = {0};
    BOOL started = CreateProcessA(
        NULL,
        command,
        NULL,
        NULL,
        TRUE,
        CREATE_NO_WINDOW,
        NULL,
        NULL,
        &startup,
        &info
    );
    assert(started != 0);

    assert(CloseHandle(write_pipe) != 0);
    assert(CloseHandle(info.hThread) != 0);

    char line[64];
    size_t used = 0;

    for (;;) {
        assert(used + 1 < sizeof(line));

        DWORD got = 0;
        assert(ReadFile(read_pipe, line + used, 1, &got, NULL) != 0);
        assert(got == 1);

        if (line[used] == '\n') {
            line[used] = '\0';
            break;
        }

        used++;
    }

    assert(CloseHandle(read_pipe) != 0);

    int port = 0;

    if (used > 5 && memcmp(line, "PORT ", 5) == 0) {
        char* end = NULL;
        long parsed = strtol(line + 5, &end, 10);

        if (end != line + 5 && (*end == '\n' || *end == '\r' || *end == '\0') && parsed > 0 &&
            parsed < 65536) {
            port = (int)parsed;
        }
    }

    assert(port > 0);
    *port_out = port;
    return info.hProcess;
}

void stop_fixture(ly_process child) {
    assert(TerminateProcess(child, 1) != 0);
    assert(WaitForSingleObject(child, INFINITE) == WAIT_OBJECT_0);
    assert(CloseHandle(child) != 0);
}
#else
ly_process spawn_fixture(const char* bytes, const char* rate, int* port_out) {
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

void stop_fixture(ly_process child) {
    kill(child, SIGKILL);
    int status = 0;
    while (waitpid(child, &status, 0) < 0 && errno == EINTR) {
    }
}
#endif

#ifdef _WIN32
char* save_env(const char* name) {
    DWORD needed = GetEnvironmentVariableA(name, NULL, 0);

    if (needed == 0) {
        return NULL;
    }

    char* copy = malloc(needed);
    assert(copy != NULL);
    assert(GetEnvironmentVariableA(name, copy, needed) == needed - 1);
    return copy;
}

void clear_env_entry(const char* name) {
    assert(SetEnvironmentVariableA(name, NULL) != 0);
}

void set_env_entry(const char* name, const char* value) {
    assert(SetEnvironmentVariableA(name, value) != 0);
}

void restore_env_entry(const char* name, char* saved) {
    if (saved == NULL) {
        clear_env_entry(name);
    } else {
        set_env_entry(name, saved);
        free(saved);
    }
}
#else
char* save_env(const char* name) {
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

void clear_env_entry(const char* name) {
    assert(unsetenv(name) == 0);
}

void set_env_entry(const char* name, const char* value) {
    assert(setenv(name, value, 1) == 0);
}

void restore_env_entry(const char* name, char* saved) {
    if (saved == NULL) {
        clear_env_entry(name);
    } else {
        set_env_entry(name, saved);
        free(saved);
    }
}
#endif

static const char* scrub_names[PROXY_SCRUB_COUNT] = {
    "http_proxy",
    "https_proxy",
    "all_proxy",
    "HTTP_PROXY",
    "HTTPS_PROXY",
    "ALL_PROXY",
};

void scrub_proxy_env(char* saved[PROXY_SCRUB_COUNT], char** saved_lower, char** saved_upper) {
    for (size_t i = 0; i < PROXY_SCRUB_COUNT; i++) {
        saved[i] = save_env(scrub_names[i]);
        clear_env_entry(scrub_names[i]);
    }

    *saved_lower = save_env("no_proxy");
    *saved_upper = save_env("NO_PROXY");
    set_env_entry("no_proxy", "*");
    set_env_entry("NO_PROXY", "*");
}

void restore_proxy_env(char* saved[PROXY_SCRUB_COUNT], char* saved_lower, char* saved_upper) {
    for (size_t i = 0; i < PROXY_SCRUB_COUNT; i++) {
        restore_env_entry(scrub_names[i], saved[i]);
    }

    restore_env_entry("no_proxy", saved_lower);
    restore_env_entry("NO_PROXY", saved_upper);
}
