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

execute_process(
  COMMAND "${LIBYTDLP_C_COMPILER}"
    "${LIBYTDLP_SOURCE_DIR}/tests/native/smoke.c"
    "-I${staged_include}"
    "-L${staged_libdir}"
    "-lytdlp"
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
