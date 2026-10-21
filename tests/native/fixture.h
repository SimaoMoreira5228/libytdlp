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

#define PROXY_SCRUB_COUNT 6

ly_process spawn_fixture(const char* bytes, const char* rate, int* port_out);
void stop_fixture(ly_process child);

char* save_env(const char* name);
void clear_env_entry(const char* name);
void set_env_entry(const char* name, const char* value);
void restore_env_entry(const char* name, char* saved);

void scrub_proxy_env(char* saved[PROXY_SCRUB_COUNT], char** saved_lower, char** saved_upper);
void restore_proxy_env(char* saved[PROXY_SCRUB_COUNT], char* saved_lower, char* saved_upper);

#endif
