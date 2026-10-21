#include "install_dirs.hpp"

#if defined(_WIN32)
#include <windows.h>

#include <string>
#include <vector>
#else
#include <dlfcn.h>

#include <string>
#endif

namespace libytdlp {
namespace {

#if defined(_WIN32)
int module_anchor_storage = 0;
#else
void module_anchor() {
}
#endif

} // namespace

#if defined(_WIN32)

std::string module_dir() {
    HMODULE handle = nullptr;

    LPCWSTR anchor = reinterpret_cast<LPCWSTR>(&module_anchor_storage);

    BOOL located = GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        anchor,
        &handle
    );

    if (located == 0) {
        return "";
    }

    if (handle == nullptr) {
        return "";
    }

    std::vector<wchar_t> buffer(512);

    DWORD length = 0;

    for (;;) {
        DWORD capacity = static_cast<DWORD>(buffer.size());

        length = GetModuleFileNameW(handle, buffer.data(), capacity);

        if (length == 0) {
            return "";
        }

        if (length < buffer.size()) {
            break;
        }

        buffer.resize(buffer.size() * 2);
    }

    int needed = WideCharToMultiByte(
        CP_UTF8,
        0,
        buffer.data(),
        static_cast<int>(length),
        nullptr,
        0,
        nullptr,
        nullptr
    );

    if (needed <= 0) {
        return "";
    }

    std::string path(static_cast<size_t>(needed), '\0');

    int written = WideCharToMultiByte(
        CP_UTF8,
        0,
        buffer.data(),
        static_cast<int>(length),
        path.data(),
        needed,
        nullptr,
        nullptr
    );

    if (written != needed) {
        return "";
    }

    std::string::size_type slash = path.find_last_of("\\/");

    if (slash == std::string::npos) {
        return "";
    }

    return path.substr(0, slash);
}

#else

std::string module_dir() {
    Dl_info info = {};

    void* symbol = reinterpret_cast<void*>(&module_anchor);

    int located = dladdr(symbol, &info);

    if (located == 0) {
        return "";
    }

    if (info.dli_fname == nullptr) {
        return "";
    }

    std::string path(info.dli_fname);

    if (path.empty()) {
        return "";
    }

    std::string::size_type slash = path.find_last_of('/');

    if (slash == std::string::npos) {
        return "";
    }

    return path.substr(0, slash);
}

#endif

} // namespace libytdlp
