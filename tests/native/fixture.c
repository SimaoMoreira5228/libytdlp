#include "fixture.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <dirent.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
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
void make_tmpdir(char* path, size_t len) {
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

void remove_tree(const char* dir) {
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
void make_tmpdir(char* path, size_t len) {
    assert(len > 32);
    snprintf(path, len, "/tmp/ly_job_XXXXXX");
    assert(mkdtemp(path) != NULL);
}

void remove_tree(const char* dir) {
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
#ifdef _WIN32
int dir_is_empty(const char* dir) {
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
int dir_is_empty(const char* dir) {
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
#ifdef _WIN32
int closed_loopback_port(void) {
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
int closed_loopback_port(void) {
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
