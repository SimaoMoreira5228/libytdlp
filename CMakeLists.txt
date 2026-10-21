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

add_library(ytdlp SHARED
    native/api.cpp
    native/bridge.cpp
    native/runtime.cpp
)
add_library(libytdlp::ytdlp ALIAS ytdlp)

target_include_directories(ytdlp
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/native
)
target_link_libraries(ytdlp PRIVATE Python::Python)
target_compile_definitions(ytdlp PRIVATE
    LIBYTDLP_BRIDGE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/python"
    LIBYTDLP_YTDLP_DIR="${CMAKE_CURRENT_SOURCE_DIR}/third_party/yt-dlp"
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
endif()

if(NOT WIN32)
    set(THREADS_PREFER_PTHREAD_FLAG ON)
    find_package(Threads REQUIRED)
    target_link_libraries(ytdlp PRIVATE Threads::Threads)
endif()

install(TARGETS ytdlp
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
endif()
