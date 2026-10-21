cmake_minimum_required(VERSION 3.22)

project(libytdlp VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

include(GNUInstallDirs)
include(CTest)

option(LIBYTDLP_SANITIZE_ADDRESS "Build with AddressSanitizer" OFF)
option(LIBYTDLP_SANITIZE_UNDEFINED "Build with UndefinedBehaviorSanitizer" OFF)
set(LIBYTDLP_DEPLOYMENT_PROFILE "FULL_BUNDLED" CACHE STRING "Dependency provisioning profile")
set_property(CACHE LIBYTDLP_DEPLOYMENT_PROFILE PROPERTY STRINGS
    FULL_BUNDLED BUNDLED_YTDLP_SYSTEM_PYTHON SYSTEM_YTDLP_SYSTEM_PYTHON)

if(NOT LIBYTDLP_DEPLOYMENT_PROFILE MATCHES
    "^(FULL_BUNDLED|BUNDLED_YTDLP_SYSTEM_PYTHON|SYSTEM_YTDLP_SYSTEM_PYTHON)$")
    message(FATAL_ERROR "LIBYTDLP_DEPLOYMENT_PROFILE must be FULL_BUNDLED, "
        "BUNDLED_YTDLP_SYSTEM_PYTHON or SYSTEM_YTDLP_SYSTEM_PYTHON")
endif()

set(LIBYTDLP_SANITIZERS "")
if(LIBYTDLP_SANITIZE_ADDRESS)
    list(APPEND LIBYTDLP_SANITIZERS address)
endif()
if(LIBYTDLP_SANITIZE_UNDEFINED)
    list(APPEND LIBYTDLP_SANITIZERS undefined)
endif()

find_package(Python COMPONENTS Interpreter Development.Embed REQUIRED)

execute_process(
    COMMAND ${Python_EXECUTABLE} -c "import yt_dlp; print(yt_dlp.version.__version__)"
    OUTPUT_VARIABLE LIBYTDLP_YTDLP_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(LIBYTDLP_YTDLP_VERSION)
    message(STATUS "yt-dlp on system Python: ${LIBYTDLP_YTDLP_VERSION}")
else()
    message(STATUS "yt-dlp not importable on system Python; tests fall back to third_party/yt-dlp")
endif()

execute_process(
    COMMAND git -C ${CMAKE_CURRENT_SOURCE_DIR}/third_party/yt-dlp rev-parse HEAD
    OUTPUT_VARIABLE LIBYTDLP_YTDLP_COMMIT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT LIBYTDLP_YTDLP_COMMIT)
    set(LIBYTDLP_YTDLP_COMMIT "unknown")
endif()
message(STATUS "Deployment profile: ${LIBYTDLP_DEPLOYMENT_PROFILE}")
message(STATUS "yt-dlp commit: ${LIBYTDLP_YTDLP_COMMIT}")

add_library(ytdlp SHARED
    native/api.cpp
    native/bridge.cpp
    native/jobs.cpp
    native/runtime.cpp
)
add_library(libytdlp::ytdlp ALIAS ytdlp)

add_library(ytdlp_static STATIC
    native/api.cpp
    native/bridge.cpp
    native/jobs.cpp
    native/runtime.cpp
)
add_library(libytdlp::ytdlp_static ALIAS ytdlp_static)

foreach(target ytdlp ytdlp_static)
    target_include_directories(${target}
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/native
    )
    target_compile_definitions(${target} PRIVATE
        LIBYTDLP_BRIDGE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/python"
        LIBYTDLP_YTDLP_DIR="${CMAKE_CURRENT_SOURCE_DIR}/third_party/yt-dlp"
        LIBYTDLP_DEPLOYMENT_PROFILE="${LIBYTDLP_DEPLOYMENT_PROFILE}"
        LIBYTDLP_YTDLP_COMMIT="${LIBYTDLP_YTDLP_COMMIT}"
    )
endforeach()
target_link_libraries(ytdlp PRIVATE Python::Python)
target_link_libraries(ytdlp_static PUBLIC Python::Python)
set_target_properties(ytdlp_static PROPERTIES
    OUTPUT_NAME ytdlp
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)
set_target_properties(ytdlp PROPERTIES
    OUTPUT_NAME ytdlp
    VERSION ${PROJECT_VERSION}
    SOVERSION 0
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)

if(LIBYTDLP_SANITIZERS)
    list(JOIN LIBYTDLP_SANITIZERS "," LIBYTDLP_SANITIZER_FLAGS)
    target_compile_options(ytdlp PRIVATE -fsanitize=${LIBYTDLP_SANITIZER_FLAGS} -fno-omit-frame-pointer)
    target_link_options(ytdlp PRIVATE -fsanitize=${LIBYTDLP_SANITIZER_FLAGS})
    target_compile_options(ytdlp_static PRIVATE -fsanitize=${LIBYTDLP_SANITIZER_FLAGS} -fno-omit-frame-pointer)
    target_link_options(ytdlp_static PUBLIC -fsanitize=${LIBYTDLP_SANITIZER_FLAGS})
endif()

if(NOT WIN32)
    set(THREADS_PREFER_PTHREAD_FLAG ON)
    find_package(Threads REQUIRED)
    target_link_libraries(ytdlp PRIVATE Threads::Threads)
    target_link_libraries(ytdlp_static PUBLIC Threads::Threads)
endif()

option(LIBYTDLP_BUILD_RUST "Build Rust bindings via Corrosion" OFF)
if(LIBYTDLP_BUILD_RUST)
    include(FetchContent)
    FetchContent_Declare(Corrosion
        GIT_REPOSITORY https://github.com/corrosion-rs/corrosion.git
        GIT_TAG 1499b14e4906a2890f5cee1547c8848db261753d
    )
    FetchContent_MakeAvailable(Corrosion)
    corrosion_import_crate(MANIFEST_PATH ${CMAKE_CURRENT_SOURCE_DIR}/bindings/rust/Cargo.toml)
endif()

install(TARGETS ytdlp ytdlp_static
    EXPORT libytdlp-targets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY include/libytdlp DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT libytdlp-targets
    NAMESPACE libytdlp::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/libytdlp
)

if(BUILD_TESTING)
    find_program(UV_EXECUTABLE NAMES uv REQUIRED)

    add_executable(ly_smoke tests/native/smoke.c)
    target_include_directories(ly_smoke PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(ly_smoke PRIVATE ytdlp Threads::Threads)
    target_compile_definitions(ly_smoke PRIVATE
        LIBYTDLP_TEST_PYTHON="${Python_EXECUTABLE}"
        LIBYTDLP_SLOW_SERVER="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/slow_server.py"
    )

    add_library(ly_header_check_c OBJECT tests/native/header_check.c)
    target_include_directories(ly_header_check_c PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

    add_library(ly_header_check_cxx OBJECT tests/native/header_check.cpp)
    target_include_directories(ly_header_check_cxx PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

    if(LIBYTDLP_SANITIZERS)
        target_compile_options(ly_smoke PRIVATE -fsanitize=${LIBYTDLP_SANITIZER_FLAGS}
                                                -fno-omit-frame-pointer)
        target_link_options(ly_smoke PRIVATE -fsanitize=${LIBYTDLP_SANITIZER_FLAGS})
    endif()

    add_test(NAME smoke COMMAND ly_smoke)
    set_tests_properties(smoke PROPERTIES ENVIRONMENT
        "LIBYTDLP_BRIDGE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/python;LIBYTDLP_YTDLP_DIR=${CMAKE_CURRENT_SOURCE_DIR}/third_party/yt-dlp"
    )

    add_test(NAME bridge
        COMMAND ${UV_EXECUTABLE} run --project ${CMAKE_CURRENT_SOURCE_DIR} python tests/python/bridge_smoke.py
    )
    set_tests_properties(bridge PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

    set(LIBYTDLP_INSTALL_VERIFY_PREFIX ${CMAKE_CURRENT_BINARY_DIR}/install-verify/prefix)
    set(LIBYTDLP_INSTALL_VERIFY_WORK ${CMAKE_CURRENT_BINARY_DIR}/install-verify/work)

    add_test(NAME install-tree
        COMMAND ${CMAKE_COMMAND}
            -DLIBYTDLP_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -DLIBYTDLP_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -DLIBYTDLP_INSTALL_PREFIX=${LIBYTDLP_INSTALL_VERIFY_PREFIX}
            -DLIBYTDLP_INSTALL_LIBDIR=${CMAKE_INSTALL_LIBDIR}
            -DLIBYTDLP_C_COMPILER=${CMAKE_C_COMPILER}
            -DLIBYTDLP_TEST_PYTHON=${Python_EXECUTABLE}
            -DLIBYTDLP_SLOW_SERVER=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/slow_server.py
            -DLIBYTDLP_BRIDGE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/python
            -DLIBYTDLP_YTDLP_DIR=${CMAKE_CURRENT_SOURCE_DIR}/third_party/yt-dlp
            -DLIBYTDLP_WORK_DIR=${LIBYTDLP_INSTALL_VERIFY_WORK}
            -DLIBYTDLP_BUILD_CONFIG=$<CONFIG>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/install_verify.cmake
    )
endif()
