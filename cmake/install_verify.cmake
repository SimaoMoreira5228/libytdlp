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

execute_process(
  COMMAND "${LIBYTDLP_C_COMPILER}"
    "${LIBYTDLP_SOURCE_DIR}/tests/native/smoke.c"
    "-I${staged_include}"
    ${LIBYTDLP_PYTHON_INCLUDES}
    "-L${staged_libdir}"
    "-lytdlp"
    ${LIBYTDLP_PYTHON_LIB}
    "-Wl,-rpath,${staged_libdir}"
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
]=])

file(WRITE "${consumer_src}/main.c" [=[
#include <stdio.h>
#include <libytdlp/libytdlp.h>

int main(void) {
    unsigned version = (unsigned)ly_abi_version();

    printf("%u\n", version);

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

execute_process(
  COMMAND "${consumer_binary}"
  RESULT_VARIABLE consumer_run_result
  OUTPUT_VARIABLE consumer_run_output
  ERROR_VARIABLE consumer_run_output
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

if(NOT consumer_run_result EQUAL 0)
  message(FATAL_ERROR "consumer run failed:\n${consumer_run_output}")
endif()

if(NOT consumer_run_output MATCHES "^[0-9]+$")
  message(FATAL_ERROR "consumer printed unexpected abi: ${consumer_run_output}")
endif()

message(STATUS "consumer find_package check ok: abi ${consumer_run_output}")
