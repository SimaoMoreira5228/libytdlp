#include "runtime.hpp"

#include <Python.h>

#include <filesystem>

#include "install_dirs.hpp"
#include "py_handle.hpp"

namespace libytdlp {
namespace {

bool prepend_path(const char* dir, std::string& error) {
    if (dir == nullptr || dir[0] == '\0') {
        return true;
    }

    std::error_code code;
    if (!std::filesystem::exists(dir, code) || code) {
        return true;
    }

    GilGuard guard;

    OwnedPyRef sys(PyImport_ImportModule("sys"));
    if (!sys) {
        error = "could not import sys";
        return false;
    }

    OwnedPyRef path(PyObject_GetAttrString(sys.get(), "path"));
    if (!path || !PyList_Check(path.get())) {
        error = "sys.path is not a list";
        return false;
    }

    OwnedPyRef dir_obj(PyUnicode_FromString(dir));
    if (!dir_obj) {
        error = "could not encode path";
        return false;
    }

    if (PyList_Insert(path.get(), 0, dir_obj.get()) != 0) {
        error = "could not prepend to sys.path";
        return false;
    }

    return true;
}

bool append_config_path(PyConfig& config, const char* dir, std::string& error) {
    if (dir == nullptr || dir[0] == '\0') {
        return true;
    }

    wchar_t* wide = Py_DecodeLocale(dir, nullptr);

    if (wide == nullptr) {
        error = "could not encode staged path";
        return false;
    }

    PyStatus list_status = PyWideStringList_Append(&config.module_search_paths, wide);
    PyMem_RawFree(wide);

    if (PyStatus_Exception(list_status)) {
        error =
            list_status.err_msg != nullptr ? list_status.err_msg : "could not record staged path";
        return false;
    }

    return true;
}

bool init_isolated(
    const char* bridge_dir,
    const char* ytdlp_dir,
    const std::string& stdlib,
    std::string& error
) {
    PyConfig config;
    PyConfig_InitIsolatedConfig(&config);

    config.site_import = 0;
    config.user_site_directory = 0;

    bool paths_ok = append_config_path(config, bridge_dir, error) &&
        append_config_path(config, ytdlp_dir, error) &&
        append_config_path(config, stdlib.c_str(), error);

    if (paths_ok) {
        std::string dynload = (std::filesystem::path(stdlib) / "lib-dynload").string();

        std::error_code code;
        if (std::filesystem::is_directory(dynload, code) && !code) {
            paths_ok = append_config_path(config, dynload.c_str(), error);
        }
    }

    if (!paths_ok) {
        PyConfig_Clear(&config);
        return false;
    }

    // NOTE: appending alone does not enable the list; without this flag
    // CPython discards it and falls back to prefix-derived paths.
    config.module_search_paths_set = 1;

    PyStatus status = Py_InitializeFromConfig(&config);
    PyConfig_Clear(&config);

    if (PyStatus_Exception(status)) {
        // NOTE: Py_ExitStatusException would terminate the host; report instead.
        error = status.err_msg != nullptr ? status.err_msg : "isolated init failed";
        return false;
    }

    PyEval_SaveThread();
    return true;
}

} // namespace

Runtime& Runtime::instance() {
    static Runtime runtime;
    return runtime;
}

std::mutex& Runtime::serial() {
    return serial_;
}

bool Runtime::host_owned() const {
    std::lock_guard<std::mutex> init(mutex_);
    return host_owned_;
}

bool Runtime::ensure(const char* bridge_dir, const char* ytdlp_dir, std::string& error) {
    std::lock_guard<std::mutex> call(serial_);
    std::lock_guard<std::mutex> init(mutex_);

    if (ready_) {
        return true;
    }

    bool owned = Py_IsInitialized() == 0;

    if (owned) {
        std::string stdlib = staged_stdlib_dir();

        if (stdlib.empty() || !has_staged_runtime()) {
            stdlib = staged_stdlib_for_dirs(bridge_dir, ytdlp_dir);
        }

        if (!stdlib.empty()) {
            if (!init_isolated(bridge_dir, ytdlp_dir, stdlib, error)) {
                return false;
            }

            host_owned_ = false;
            ready_ = true;
            return true;
        }
    }

    if (owned) {
        Py_Initialize();
    }

    bool ok = prepend_path(ytdlp_dir, error) && prepend_path(bridge_dir, error);

    if (owned) {
        PyEval_SaveThread();
    }

    if (!ok) {
        return false;
    }

    host_owned_ = !owned;
    ready_ = true;
    return true;
}

} // namespace libytdlp
