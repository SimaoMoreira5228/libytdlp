#include "runtime.hpp"

#include <Python.h>

#include <filesystem>

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
