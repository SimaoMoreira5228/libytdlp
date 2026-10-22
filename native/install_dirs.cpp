#include "install_dirs.hpp"

#if defined(_WIN32)
#include <windows.h>

#include <string>
#include <vector>
#else
#include <dlfcn.h>

#include <string>
#endif

#include <filesystem>

namespace libytdlp {
namespace {

#if defined(_WIN32)
int module_anchor_storage = 0;
#else
void module_anchor() {
}
#endif

bool marker_present(const std::filesystem::path& marker) {
    std::error_code code;

    return std::filesystem::exists(marker, code) && !code;
}

bool root_is_complete(const std::filesystem::path& root) {
    if (!marker_present(root / "python-stdlib" / "os.py")) {
        return false;
    }

    if (!marker_present(root / "python" / "libytdlp_bridge" / "__init__.py")) {
        return false;
    }

    return marker_present(root / "yt-dlp" / "yt_dlp" / "__init__.py");
}

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

std::string staged_share_dir() {
    static const std::string cached = [] {
        std::string self = module_dir();

        if (self.empty()) {
            return std::string();
        }

        std::filesystem::path candidate = std::filesystem::path(self) / ".." / "share" / "libytdlp";

        std::filesystem::path marker = candidate / "python" / "libytdlp_bridge" / "__init__.py";

        std::error_code code;

        bool present = std::filesystem::exists(marker, code);

        if (code) {
            return std::string();
        }

        if (!present) {
            return std::string();
        }

        return candidate.lexically_normal().string();
    }();

    return cached;
}

std::string staged_stdlib_dir() {
    std::string share = staged_share_dir();

    if (share.empty()) {
        return std::string();
    }

    return (std::filesystem::path(share) / "python-stdlib").string();
}

bool has_staged_runtime() {
    std::string share = staged_share_dir();

    if (share.empty()) {
        return false;
    }

    return root_is_complete(std::filesystem::path(share));
}

std::string staged_stdlib_for_dirs(const char* bridge_dir, const char* ytdlp_dir) {
    if (bridge_dir == nullptr || bridge_dir[0] == '\0') {
        return std::string();
    }

    if (ytdlp_dir == nullptr || ytdlp_dir[0] == '\0') {
        return std::string();
    }

    std::filesystem::path bridge(bridge_dir);

    if (!marker_present(bridge / "libytdlp_bridge" / "__init__.py")) {
        return std::string();
    }

    if (!marker_present(std::filesystem::path(ytdlp_dir) / "yt_dlp" / "__init__.py")) {
        return std::string();
    }

    std::filesystem::path root = bridge.parent_path();

    if (!root_is_complete(root)) {
        return std::string();
    }

    return (root / "python-stdlib").string();
}

} // namespace libytdlp
