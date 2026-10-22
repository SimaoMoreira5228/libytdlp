#ifndef LIBYTDLP_TEST_FIXTURE_H
#define LIBYTDLP_TEST_FIXTURE_H

#include <stddef.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
typedef HANDLE ly_process;
#else
#include <sys/types.h>
typedef pid_t ly_process;
#endif

ly_process spawn_fixture(const char* bytes, const char* rate, int* port_out);
void stop_fixture(ly_process child);

#endif
