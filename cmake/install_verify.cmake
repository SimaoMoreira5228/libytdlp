if(NOT DEFINED LIBYTDLP_SOURCE_DIR)
  message(FATAL_ERROR "LIBYTDLP_SOURCE_DIR is not set")
endif()

if(NOT DEFINED LIBYTDLP_BUILD_DIR)
  message(FATAL_ERROR "LIBYTDLP_BUILD_DIR is not set")
endif()

if(NOT DEFINED LIBYTDLP_INSTALL_PREFIX)
  message(FATAL_ERROR "LIBYTDLP_INSTALL_PREFIX is not set")
endif()

if(NOT DEFINED LIBYTDLP_INSTALL_LIBDIR)
  message(FATAL_ERROR "LIBYTDLP_INSTALL_LIBDIR is not set")
endif()

if(NOT DEFINED LIBYTDLP_C_COMPILER)
  message(FATAL_ERROR "LIBYTDLP_C_COMPILER is not set")
endif()

if(NOT DEFINED LIBYTDLP_WORK_DIR)
  message(FATAL_ERROR "LIBYTDLP_WORK_DIR is not set")
endif()

if(NOT DEFINED LIBYTDLP_PYTHON_INCLUDES)
  message(FATAL_ERROR "LIBYTDLP_PYTHON_INCLUDES is not set")
endif()

if(NOT DEFINED LIBYTDLP_PYTHON_LIB)
  message(FATAL_ERROR "LIBYTDLP_PYTHON_LIB is not set")
endif()

if(NOT DEFINED LIBYTDLP_PYTHON_LIBDIR)
  message(FATAL_ERROR "LIBYTDLP_PYTHON_LIBDIR is not set")
endif()

if(NOT DEFINED LIBYTDLP_PROJECT_VERSION)
  message(FATAL_ERROR "LIBYTDLP_PROJECT_VERSION is not set")
endif()

if(IS_ABSOLUTE "${LIBYTDLP_INSTALL_LIBDIR}")
  set(staged_libdir "${LIBYTDLP_INSTALL_LIBDIR}")
else()
  set(staged_libdir "${LIBYTDLP_INSTALL_PREFIX}/${LIBYTDLP_INSTALL_LIBDIR}")
endif()

set(staged_include "${LIBYTDLP_INSTALL_PREFIX}/include")
set(staged_binary "${LIBYTDLP_WORK_DIR}/ly_smoke_staged")

file(REMOVE_RECURSE "${LIBYTDLP_INSTALL_PREFIX}")
file(REMOVE_RECURSE "${LIBYTDLP_WORK_DIR}")
file(MAKE_DIRECTORY "${LIBYTDLP_WORK_DIR}")

set(install_args
  --install
  "${LIBYTDLP_BUILD_DIR}"
  --prefix
  "${LIBYTDLP_INSTALL_PREFIX}"
)

if(LIBYTDLP_BUILD_CONFIG)
  list(APPEND install_args --config "${LIBYTDLP_BUILD_CONFIG}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" ${install_args}
  RESULT_VARIABLE install_result
  OUTPUT_VARIABLE install_output
  ERROR_VARIABLE install_output
)

if(NOT install_result EQUAL 0)
  message(FATAL_ERROR "cmake --install failed:\n${install_output}")
endif()

if(NOT EXISTS "${staged_include}/libytdlp/libytdlp.h")
  message(FATAL_ERROR "staged header not found under ${staged_include}")
endif()

file(GLOB staged_libs "${staged_libdir}/libytdlp.*")

if(NOT staged_libs)
  message(FATAL_ERROR "staged library not found under ${staged_libdir}")
endif()

if(NOT EXISTS "${staged_libdir}/cmake/libytdlp/libytdlp-targets.cmake")
  message(FATAL_ERROR "staged CMake export not found under ${staged_libdir}")
endif()

if(NOT EXISTS "${staged_libdir}/cmake/libytdlp/libytdlp-config.cmake")
  message(FATAL_ERROR "staged CMake package config not found under ${staged_libdir}")
endif()

if(NOT EXISTS "${staged_libdir}/cmake/libytdlp/libytdlp-config-version.cmake")
  message(FATAL_ERROR "staged CMake package version not found under ${staged_libdir}")
endif()

file(GLOB staged_static "${staged_libdir}/libytdlp.a")

if(NOT staged_static)
  message(FATAL_ERROR "staged static archive not found under ${staged_libdir}")
endif()

if(NOT EXISTS "${LIBYTDLP_INSTALL_PREFIX}/share/libytdlp/python/libytdlp_bridge/__init__.py")
  message(FATAL_ERROR "staged python bridge not found under ${LIBYTDLP_INSTALL_PREFIX}/share/libytdlp")
endif()

if(NOT EXISTS "${LIBYTDLP_INSTALL_PREFIX}/share/libytdlp/yt-dlp/yt_dlp/__init__.py")
  message(FATAL_ERROR "staged yt-dlp sources not found under ${LIBYTDLP_INSTALL_PREFIX}/share/libytdlp")
endif()

if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
  if(NOT EXISTS "${LIBYTDLP_INSTALL_PREFIX}/share/libytdlp/python-stdlib/os.py")
    message(FATAL_ERROR "staged python stdlib not found under ${LIBYTDLP_INSTALL_PREFIX}/share/libytdlp")
  endif()

  if(NOT EXISTS "${LIBYTDLP_INSTALL_PREFIX}/share/libytdlp/runtime.json")
    message(FATAL_ERROR "staged runtime record not found under ${LIBYTDLP_INSTALL_PREFIX}/share/libytdlp")
  endif()

  file(READ "${LIBYTDLP_INSTALL_PREFIX}/share/libytdlp/runtime.json" staged_runtime_record)

  if(NOT staged_runtime_record MATCHES "libpython")
    message(FATAL_ERROR "staged runtime record has no libpython entry:\n${staged_runtime_record}")
  endif()
endif()

foreach(staged_doc LICENSE NOTICE THIRD_PARTY_NOTICES.md)
  if(NOT EXISTS "${LIBYTDLP_INSTALL_PREFIX}/share/doc/libytdlp/${staged_doc}")
    message(FATAL_ERROR "staged ${staged_doc} not found under ${LIBYTDLP_INSTALL_PREFIX}/share/doc/libytdlp")
  endif()
endforeach()

set(staged_sbom "${LIBYTDLP_INSTALL_PREFIX}/share/libytdlp/sbom.json")

if(NOT EXISTS "${staged_sbom}")
  message(FATAL_ERROR "staged SBOM not found at ${staged_sbom}")
endif()

execute_process(
  COMMAND "${LIBYTDLP_TEST_PYTHON}" "${LIBYTDLP_SOURCE_DIR}/cmake/install_verify_sbom.py"
    "${staged_sbom}" "${LIBYTDLP_PROJECT_VERSION}"
  RESULT_VARIABLE sbom_check_result
  OUTPUT_VARIABLE sbom_check_output
  ERROR_VARIABLE sbom_check_output
)

if(NOT sbom_check_result EQUAL 0)
  message(FATAL_ERROR "staged SBOM check failed:\n${sbom_check_output}")
endif()

file(GLOB_RECURSE staged_ffmpeg
  "${LIBYTDLP_INSTALL_PREFIX}/ffmpeg"
  "${LIBYTDLP_INSTALL_PREFIX}/ffmpeg.exe"
  "${LIBYTDLP_INSTALL_PREFIX}/ffprobe"
  "${LIBYTDLP_INSTALL_PREFIX}/ffprobe.exe"
  "${LIBYTDLP_INSTALL_PREFIX}/libav*"
)

if(staged_ffmpeg)
  message(FATAL_ERROR "staged FFmpeg artifacts must not ship: ${staged_ffmpeg}")
endif()

execute_process(
  COMMAND "${LIBYTDLP_C_COMPILER}"
    "${LIBYTDLP_SOURCE_DIR}/tests/native/smoke.c"
    "${LIBYTDLP_SOURCE_DIR}/tests/native/fixture.c"
    "-I${staged_include}"
    ${LIBYTDLP_PYTHON_INCLUDES}
    "-L${staged_libdir}"
    "-lytdlp"
    ${LIBYTDLP_PYTHON_LIB}
    "-Wl,-rpath,${staged_libdir}"
    "-Wl,-rpath,${LIBYTDLP_PYTHON_LIBDIR}"
    "-DLIBYTDLP_TEST_PYTHON=\"${LIBYTDLP_TEST_PYTHON}\""
    "-DLIBYTDLP_SLOW_SERVER=\"${LIBYTDLP_SLOW_SERVER}\""
    "-pthread"
    -o "${staged_binary}"
  RESULT_VARIABLE compile_result
  OUTPUT_VARIABLE compile_output
  ERROR_VARIABLE compile_output
)

if(NOT compile_result EQUAL 0)
  message(FATAL_ERROR "staged smoke compile failed:\n${compile_output}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
    "LIBYTDLP_BRIDGE_DIR=${LIBYTDLP_BRIDGE_DIR}"
    "LIBYTDLP_YTDLP_DIR=${LIBYTDLP_YTDLP_DIR}"
    "${staged_binary}"
  RESULT_VARIABLE run_result
  OUTPUT_VARIABLE run_output
  ERROR_VARIABLE run_output
)

if(NOT run_result EQUAL 0)
  message(FATAL_ERROR "staged smoke run failed:\n${run_output}")
endif()

message(STATUS "staged smoke output:\n${run_output}")

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
    --unset=LIBYTDLP_BRIDGE_DIR
    --unset=LIBYTDLP_YTDLP_DIR
    "${staged_binary}"
  RESULT_VARIABLE run_bare_result
  OUTPUT_VARIABLE run_bare_output
  ERROR_VARIABLE run_bare_output
)

if(NOT run_bare_result EQUAL 0)
  message(FATAL_ERROR "staged smoke run without env failed:\n${run_bare_output}")
endif()

message(STATUS "staged smoke output without env:\n${run_bare_output}")

if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
  execute_process(
    COMMAND env -i PATH=/usr/bin:/bin "${staged_binary}"
    RESULT_VARIABLE run_scrubbed_result
    OUTPUT_VARIABLE run_scrubbed_output
    ERROR_VARIABLE run_scrubbed_output
  )

  if(NOT run_scrubbed_result EQUAL 0)
    message(FATAL_ERROR "staged smoke run with scrubbed env failed:\n${run_scrubbed_output}")
  endif()

  message(STATUS "staged smoke output with scrubbed env:\n${run_scrubbed_output}")

  set(staged_probe_src "${LIBYTDLP_WORK_DIR}/isolate_probe.c")
  set(staged_probe "${LIBYTDLP_WORK_DIR}/ly_isolate_probe")

  file(WRITE "${staged_probe_src}" [=[
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libytdlp/libytdlp.h>

int main(void) {
    ly_result* info = NULL;
    ly_error* error = NULL;

    if (ly_version_info(&info, &error) != LY_OK) {
        fprintf(stderr, "version info failed\n");
        return 1;
    }

    const char* data = NULL;
    size_t len = 0;

    if (ly_result_json(info, &data, &len) != LY_OK || data == NULL) {
        fprintf(stderr, "version json failed\n");
        ly_result_release(info);
        return 1;
    }

    char* text = (char*)malloc(len + 1);
    memcpy(text, data, len);
    text[len] = '\0';

    int rc = 0;

    if (strstr(text, "\"isolated\":true") == NULL) {
        fprintf(stderr, "interpreter is not isolated: %s\n", text);
        rc = 1;
    }

    if (strstr(text, "\"host_owned\":false") == NULL) {
        fprintf(stderr, "expected owned interpreter: %s\n", text);
        rc = 1;
    }

    if (strstr(text, LIBYTDLP_STAGED_PREFIX) == NULL) {
        fprintf(stderr, "staged prefix missing from version info\n");
        rc = 1;
    }

    const char* paths = strstr(text, "\"sys_path\":");

    if (paths == NULL) {
        fprintf(stderr, "version info has no sys_path\n");
        rc = 1;
    } else {
        const char* open = strchr(paths, '[');
        const char* shut = open != NULL ? strchr(open, ']') : NULL;

        if (open == NULL || shut == NULL || shut < open) {
            fprintf(stderr, "sys_path is not a list\n");
            rc = 1;
        } else {
            size_t seglen = (size_t)(shut - open);
            char* seg = (char*)malloc(seglen + 1);
            memcpy(seg, open, seglen);
            seg[seglen] = '\0';

            if (strstr(seg, "site-packages") != NULL) {
                fprintf(stderr, "sys_path leaks site-packages: %s\n", seg);
                rc = 1;
            }

            if (strstr(seg, "/usr/lib") != NULL || strstr(seg, "/usr/local/lib") != NULL) {
                fprintf(stderr, "sys_path leaks system lib: %s\n", seg);
                rc = 1;
            }

            free(seg);
        }
    }

    if (rc == 0) {
        printf("isolation probe ok\n");
    }

    free(text);
    ly_result_release(info);
    return rc;
}
]=])

  execute_process(
    COMMAND "${LIBYTDLP_C_COMPILER}"
      "${staged_probe_src}"
      "-I${staged_include}"
      ${LIBYTDLP_PYTHON_INCLUDES}
      "-L${staged_libdir}"
      "-lytdlp"
      ${LIBYTDLP_PYTHON_LIB}
      "-Wl,-rpath,${staged_libdir}"
      "-Wl,-rpath,${LIBYTDLP_PYTHON_LIBDIR}"
      "-DLIBYTDLP_STAGED_PREFIX=\"${LIBYTDLP_INSTALL_PREFIX}\""
      "-o" "${staged_probe}"
    RESULT_VARIABLE probe_compile_result
    OUTPUT_VARIABLE probe_compile_output
    ERROR_VARIABLE probe_compile_output
  )

  if(NOT probe_compile_result EQUAL 0)
    message(FATAL_ERROR "isolation probe compile failed:\n${probe_compile_output}")
  endif()

  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
      --unset=LIBYTDLP_BRIDGE_DIR
      --unset=LIBYTDLP_YTDLP_DIR
      "${staged_probe}"
    RESULT_VARIABLE probe_run_result
    OUTPUT_VARIABLE probe_run_output
    ERROR_VARIABLE probe_run_output
  )

  if(NOT probe_run_result EQUAL 0)
    message(FATAL_ERROR "isolation probe run failed:\n${probe_run_output}")
  endif()

  message(STATUS "isolation probe output:\n${probe_run_output}")

  execute_process(
    COMMAND env -i PATH=/usr/bin:/bin "${staged_probe}"
    RESULT_VARIABLE probe_scrubbed_result
    OUTPUT_VARIABLE probe_scrubbed_output
    ERROR_VARIABLE probe_scrubbed_output
  )

  if(NOT probe_scrubbed_result EQUAL 0)
    message(FATAL_ERROR "isolation probe run with scrubbed env failed:\n${probe_scrubbed_output}")
  endif()

  message(STATUS "isolation probe output with scrubbed env:\n${probe_scrubbed_output}")
endif()

set(consumer_src "${LIBYTDLP_WORK_DIR}/consumer")
set(consumer_build "${LIBYTDLP_WORK_DIR}/consumer-build")

file(REMOVE_RECURSE "${consumer_src}")
file(REMOVE_RECURSE "${consumer_build}")
file(MAKE_DIRECTORY "${consumer_src}")

file(WRITE "${consumer_src}/CMakeLists.txt" [=[
cmake_minimum_required(VERSION 3.22)
project(ly_consumer C)
find_package(libytdlp REQUIRED)
add_executable(consumer main.c)
target_link_libraries(consumer PRIVATE libytdlp::ytdlp)
add_executable(consumer_static main.c)
target_link_libraries(consumer_static PRIVATE libytdlp::ytdlp_static)
]=])

file(WRITE "${consumer_src}/main.c" [=[
#include <stdio.h>
#include <string.h>
#include <libytdlp/libytdlp.h>

int main(void) {
    unsigned version = (unsigned)ly_abi_version();

    printf("%u\n", version);

    ly_result* info = NULL;
    ly_error* error = NULL;

    if (ly_version_info(&info, &error) != LY_OK) {
        fprintf(stderr, "version info failed: %s\n", error ? ly_error_message(error) : "no detail");
        ly_error_release(error);
        return 1;
    }

    const char* data = NULL;
    size_t len = 0;

    if (ly_result_json(info, &data, &len) != LY_OK || data == NULL) {
        fprintf(stderr, "version json failed\n");
        ly_result_release(info);
        return 1;
    }

    if (strstr(data, "deployment_profile") == NULL) {
        fprintf(stderr, "version json has no deployment profile: %.*s\n", (int)len, data);
        ly_result_release(info);
        return 1;
    }

    printf("%.*s\n", (int)len, data);
    ly_result_release(info);

    return 0;
}
]=])

execute_process(
  COMMAND "${CMAKE_COMMAND}" -S "${consumer_src}" -B "${consumer_build}"
    "-DCMAKE_PREFIX_PATH=${LIBYTDLP_INSTALL_PREFIX}"
    "-DCMAKE_C_COMPILER=${LIBYTDLP_C_COMPILER}"
    "-DPython_EXECUTABLE=${LIBYTDLP_TEST_PYTHON}"
  RESULT_VARIABLE consumer_configure_result
  OUTPUT_VARIABLE consumer_configure_output
  ERROR_VARIABLE consumer_configure_output
)

if(NOT consumer_configure_result EQUAL 0)
  message(FATAL_ERROR "consumer configure failed:\n${consumer_configure_output}")
endif()

set(consumer_build_args --build "${consumer_build}")

if(LIBYTDLP_BUILD_CONFIG)
  list(APPEND consumer_build_args --config "${LIBYTDLP_BUILD_CONFIG}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" ${consumer_build_args}
  RESULT_VARIABLE consumer_build_result
  OUTPUT_VARIABLE consumer_build_output
  ERROR_VARIABLE consumer_build_output
)

if(NOT consumer_build_result EQUAL 0)
  message(FATAL_ERROR "consumer build failed:\n${consumer_build_output}")
endif()

file(GLOB_RECURSE consumer_candidates "${consumer_build}/consumer${CMAKE_EXECUTABLE_SUFFIX}")

list(LENGTH consumer_candidates consumer_candidate_count)

if(NOT consumer_candidate_count GREATER 0)
  message(FATAL_ERROR "consumer binary not found under ${consumer_build}")
endif()

list(GET consumer_candidates 0 consumer_binary)

file(GLOB_RECURSE consumer_static_candidates "${consumer_build}/consumer_static${CMAKE_EXECUTABLE_SUFFIX}")

list(LENGTH consumer_static_candidates consumer_static_candidate_count)

if(NOT consumer_static_candidate_count GREATER 0)
  message(FATAL_ERROR "static consumer binary not found under ${consumer_build}")
endif()

list(GET consumer_static_candidates 0 consumer_static_binary)

foreach(consumer_target "${consumer_binary}" "${consumer_static_binary}")
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
      --unset=LIBYTDLP_BRIDGE_DIR
      --unset=LIBYTDLP_YTDLP_DIR
      "${consumer_target}"
    RESULT_VARIABLE consumer_run_result
    OUTPUT_VARIABLE consumer_run_output
    ERROR_VARIABLE consumer_run_output
  )

  if(NOT consumer_run_result EQUAL 0)
    message(FATAL_ERROR "consumer run failed:\n${consumer_run_output}")
  endif()

  if(NOT consumer_run_output MATCHES "^[0-9]+\n.*deployment_profile")
    message(FATAL_ERROR "consumer printed unexpected output: ${consumer_run_output}")
  endif()

  message(STATUS "consumer find_package check ok (${consumer_target}):\n${consumer_run_output}")
endforeach()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
    "LIBYTDLP_BRIDGE_DIR=${LIBYTDLP_INSTALL_PREFIX}/share/libytdlp/python"
    "LIBYTDLP_YTDLP_DIR=${LIBYTDLP_INSTALL_PREFIX}/share/libytdlp/yt-dlp"
    "${consumer_static_binary}"
  RESULT_VARIABLE consumer_static_env_result
  OUTPUT_VARIABLE consumer_static_env_output
  ERROR_VARIABLE consumer_static_env_output
)

if(NOT consumer_static_env_result EQUAL 0)
  message(FATAL_ERROR "static consumer run with staged env failed:\n${consumer_static_env_output}")
endif()

if(NOT consumer_static_env_output MATCHES "\"isolated\":true")
  message(FATAL_ERROR "static consumer with staged env is not isolated:\n${consumer_static_env_output}")
endif()

message(STATUS "static consumer staged-env isolation ok:\n${consumer_static_env_output}")
