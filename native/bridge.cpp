#include "bridge.hpp"

#include <Python.h>

#include <mutex>

#include "py_handle.hpp"
#include "runtime.hpp"

namespace libytdlp {
namespace {

ly_status map_exception(const std::string& name) {
    if (name == "ValueError" || name == "TypeError" || name == "JSONDecodeError") {
        return LY_E_INVALID_ARGUMENT;
    }

    if (name == "UnsupportedError" || name == "NoSupportingHandlers" || name == "ExtractorError" ||
        name == "DownloadError") {
        return LY_E_EXTRACT;
    }

    if (name == "ImportError" || name == "AttributeError") {
        return LY_E_RUNTIME;
    }

    return LY_E_RUNTIME;
}

BridgeResult call_bridge(const std::string& url, const std::string& options) {
    BridgeResult out;

    if (url.size() > static_cast<size_t>(PY_SSIZE_T_MAX) ||
        options.size() > static_cast<size_t>(PY_SSIZE_T_MAX)) {
        out.status = LY_E_INVALID_ARGUMENT;
        out.message = "input too large";
        return out;
    }

    OwnedPyRef module(PyImport_ImportModule("libytdlp_bridge.extract"));
    if (!module) {
        PyErr_Clear();
        out.message = "could not import libytdlp_bridge.extract";
        return out;
    }

    OwnedPyRef function(PyObject_GetAttrString(module.get(), "extract_json"));
    if (!function || !PyCallable_Check(function.get())) {
        PyErr_Clear();
        out.message = "bridge entry point extract_json is missing";
        return out;
    }

    OwnedPyRef url_obj(
        PyUnicode_FromStringAndSize(url.data(), static_cast<Py_ssize_t>(url.size()))
    );
    OwnedPyRef options_obj(
        PyUnicode_FromStringAndSize(options.data(), static_cast<Py_ssize_t>(options.size()))
    );
    if (!url_obj || !options_obj) {
        PyErr_Clear();
        out.message = "could not encode call arguments";
        return out;
    }

    OwnedPyRef result(
        PyObject_CallFunctionObjArgs(function.get(), url_obj.get(), options_obj.get(), nullptr)
    );
    if (!result) {
        PyObject* raw_type = nullptr;
        PyObject* raw_value = nullptr;
        PyObject* raw_traceback = nullptr;
        PyErr_Fetch(&raw_type, &raw_value, &raw_traceback);
        OwnedPyRef type(raw_type);
        OwnedPyRef value(raw_value);
        OwnedPyRef traceback(raw_traceback);

        std::string name = "PythonError";
        std::string detail;

        if (type) {
            OwnedPyRef type_name(PyObject_GetAttrString(type.get(), "__name__"));
            if (!type_name) {
                PyErr_Clear();
            } else {
                const char* text = PyUnicode_AsUTF8(type_name.get());
                if (text == nullptr) {
                    PyErr_Clear();
                } else {
                    name = text;
                }
            }
        }

        if (value) {
            OwnedPyRef text(PyObject_Str(value.get()));
            if (!text) {
                PyErr_Clear();
            } else {
                const char* raw = PyUnicode_AsUTF8(text.get());
                if (raw == nullptr) {
                    PyErr_Clear();
                } else {
                    detail = raw;
                }
            }
        }
        PyErr_Clear();

        out.status = map_exception(name);
        out.message = name;
        if (!detail.empty()) {
            out.message += ": ";
            out.message += detail;
        }
        return out;
    }

    if (!PyUnicode_Check(result.get())) {
        out.message = "bridge returned a non-string result";
        return out;
    }

    Py_ssize_t size = 0;
    const char* data = PyUnicode_AsUTF8AndSize(result.get(), &size);
    if (data == nullptr) {
        PyErr_Clear();
        out.message = "could not decode bridge result";
        return out;
    }

    out.ok = true;
    out.status = LY_OK;
    out.json.assign(data, static_cast<size_t>(size));
    return out;
}

} // namespace

BridgeResult extract_json(const std::string& url, const std::string& options) {
    std::lock_guard<std::mutex> lock(Runtime::instance().serial());
    GilGuard guard;

    return call_bridge(url, options);
}

} // namespace libytdlp
