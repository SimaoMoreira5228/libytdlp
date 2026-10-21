#include "bridge.hpp"

#include <Python.h>

#include <mutex>
#include <string>
#include <vector>

#include "py_handle.hpp"
#include "runtime.hpp"

namespace libytdlp {
namespace {

const char* kNetworkClasses[] = {
    "URLError",
    "HTTPError",
    "SSLError",
    "SSLZeroReturnError",
    "SSLCertVerificationError",
    "SSLEOFError",
    "ConnectionError",
    "ConnectionRefusedError",
    "ConnectionResetError",
    "ConnectionAbortedError",
    "BrokenPipeError",
    "TimeoutError",
    "gaierror",
    "HTTPException",
    "RemoteDisconnected",
    "IncompleteRead",
    "BadStatusLine",
    "ReadTimeoutError",
    "ConnectTimeoutError",
    "NewConnectionError",
    "MaxRetryError",
    "NameResolutionError",
    "ProxyError",
    "CurlError",
};

bool is_network_class(const std::string& name) {
    for (const char* candidate : kNetworkClasses) {
        if (name == candidate) {
            return true;
        }
    }

    return false;
}

std::string class_name_of_type(PyObject* type) {
    OwnedPyRef name_attr(PyObject_GetAttrString(type, "__name__"));

    if (!name_attr) {
        PyErr_Clear();
        return "PythonError";
    }

    const char* text = PyUnicode_AsUTF8(name_attr.get());

    if (text == nullptr) {
        PyErr_Clear();
        return "PythonError";
    }

    return text;
}

ly_status map_chain(const std::vector<std::string>& names) {
    if (names.empty()) {
        return LY_E_RUNTIME;
    }

    const std::string& top = names.front();

    if (top == "_Cancelled") {
        return LY_E_CANCELLED;
    }

    for (const std::string& name : names) {
        if (is_network_class(name)) {
            return LY_E_NETWORK;
        }
    }

    if (top == "ValueError" || top == "TypeError" || top == "JSONDecodeError") {
        return LY_E_INVALID_ARGUMENT;
    }

    if (top == "UnsupportedError" || top == "NoSupportingHandlers" || top == "ExtractorError" ||
        top == "DownloadError") {
        return LY_E_EXTRACT;
    }

    if (top == "ImportError" || top == "AttributeError") {
        return LY_E_RUNTIME;
    }

    return LY_E_RUNTIME;
}

void capture_failure(BridgeResult& out) {
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

    std::vector<std::string> chain;
    chain.push_back(name);

    if (value) {
        OwnedPyRef anchor;
        PyObject* current = value.get();

        for (int depth = 0; depth < 8 && current != nullptr; ++depth) {
            OwnedPyRef cause(PyObject_GetAttrString(current, "__cause__"));

            if (!cause) {
                PyErr_Clear();
            }

            OwnedPyRef next;

            if (cause && cause.get() != Py_None) {
                next = std::move(cause);
            } else {
                OwnedPyRef context(PyObject_GetAttrString(current, "__context__"));

                if (!context) {
                    PyErr_Clear();
                } else if (context.get() != Py_None) {
                    next = std::move(context);
                }
            }

            if (!next) {
                break;
            }

            PyObject* next_type = reinterpret_cast<PyObject*>(Py_TYPE(next.get()));
            chain.push_back(class_name_of_type(next_type));

            anchor = std::move(next);
            current = anchor.get();
        }
    }

    PyErr_Clear();

    out.status = map_chain(chain);
    out.exception_class = name;
    out.message = name;
    if (!detail.empty()) {
        out.message += ": ";
        out.message += detail;
    }
}

BridgeResult call_bridge(
    const std::string& url,
    const std::string& context_options,
    const std::string& call_options
) {
    BridgeResult out;

    if (url.size() > static_cast<size_t>(PY_SSIZE_T_MAX) ||
        context_options.size() > static_cast<size_t>(PY_SSIZE_T_MAX) ||
        call_options.size() > static_cast<size_t>(PY_SSIZE_T_MAX)) {
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
    OwnedPyRef context_obj(PyUnicode_FromStringAndSize(
        context_options.data(),
        static_cast<Py_ssize_t>(context_options.size())
    ));
    OwnedPyRef call_obj(PyUnicode_FromStringAndSize(
        call_options.data(),
        static_cast<Py_ssize_t>(call_options.size())
    ));
    if (!url_obj || !context_obj || !call_obj) {
        PyErr_Clear();
        out.message = "could not encode call arguments";
        return out;
    }

    OwnedPyRef result(PyObject_CallFunctionObjArgs(
        function.get(),
        url_obj.get(),
        context_obj.get(),
        call_obj.get(),
        nullptr
    ));
    if (!result) {
        capture_failure(out);
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

BridgeResult call_download(
    const std::string& url,
    const std::string& context_options,
    const std::string& job_options,
    PyObject* events,
    PyObject* cancel_event
) {
    BridgeResult out;

    if (url.size() > static_cast<size_t>(PY_SSIZE_T_MAX) ||
        context_options.size() > static_cast<size_t>(PY_SSIZE_T_MAX) ||
        job_options.size() > static_cast<size_t>(PY_SSIZE_T_MAX)) {
        out.status = LY_E_INVALID_ARGUMENT;
        out.message = "input too large";
        return out;
    }

    if (events == nullptr || cancel_event == nullptr) {
        out.status = LY_E_INTERNAL;
        out.message = "download plumbing is missing";
        return out;
    }

    OwnedPyRef module(PyImport_ImportModule("libytdlp_bridge.download"));
    if (!module) {
        PyErr_Clear();
        out.message = "could not import libytdlp_bridge.download";
        return out;
    }

    OwnedPyRef function(PyObject_GetAttrString(module.get(), "download_json"));
    if (!function || !PyCallable_Check(function.get())) {
        PyErr_Clear();
        out.message = "bridge entry point download_json is missing";
        return out;
    }

    OwnedPyRef url_obj(
        PyUnicode_FromStringAndSize(url.data(), static_cast<Py_ssize_t>(url.size()))
    );
    OwnedPyRef context_obj(PyUnicode_FromStringAndSize(
        context_options.data(),
        static_cast<Py_ssize_t>(context_options.size())
    ));
    OwnedPyRef job_obj(
        PyUnicode_FromStringAndSize(job_options.data(), static_cast<Py_ssize_t>(job_options.size()))
    );
    if (!url_obj || !context_obj || !job_obj) {
        PyErr_Clear();
        out.message = "could not encode call arguments";
        return out;
    }

    OwnedPyRef result(PyObject_CallFunctionObjArgs(
        function.get(),
        url_obj.get(),
        context_obj.get(),
        job_obj.get(),
        events,
        cancel_event,
        nullptr
    ));
    if (!result) {
        capture_failure(out);
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

BridgeResult runtime_info_json(
    const std::string& profile,
    const std::string& commit,
    uint32_t abi,
    bool host_owned
) {
    BridgeResult out;

    if (profile.size() > static_cast<size_t>(PY_SSIZE_T_MAX) ||
        commit.size() > static_cast<size_t>(PY_SSIZE_T_MAX)) {
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

    OwnedPyRef function(PyObject_GetAttrString(module.get(), "runtime_info"));
    if (!function || !PyCallable_Check(function.get())) {
        PyErr_Clear();
        out.message = "bridge entry point runtime_info is missing";
        return out;
    }

    OwnedPyRef profile_obj(
        PyUnicode_FromStringAndSize(profile.data(), static_cast<Py_ssize_t>(profile.size()))
    );
    OwnedPyRef commit_obj(
        PyUnicode_FromStringAndSize(commit.data(), static_cast<Py_ssize_t>(commit.size()))
    );
    OwnedPyRef abi_obj(PyLong_FromUnsignedLong(abi));
    OwnedPyRef owned_obj(PyBool_FromLong(host_owned ? 1 : 0));
    if (!profile_obj || !commit_obj || !abi_obj || !owned_obj) {
        PyErr_Clear();
        out.message = "could not encode call arguments";
        return out;
    }

    OwnedPyRef result(PyObject_CallFunctionObjArgs(
        function.get(),
        profile_obj.get(),
        commit_obj.get(),
        abi_obj.get(),
        owned_obj.get(),
        nullptr
    ));
    if (!result) {
        PyErr_Clear();
        out.message = "runtime_info call failed";
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

BridgeResult extract_json(
    const std::string& url,
    const std::string& context_options,
    const std::string& call_options
) {
    std::lock_guard<std::mutex> lock(Runtime::instance().serial());
    GilGuard guard;

    return call_bridge(url, context_options, call_options);
}

BridgeResult download_json(
    const std::string& url,
    const std::string& context_options,
    const std::string& job_options,
    PyObject* events,
    PyObject* cancel_event
) {
    return call_download(url, context_options, job_options, events, cancel_event);
}

BridgeResult runtime_info_json() {
    std::lock_guard<std::mutex> lock(Runtime::instance().serial());

    std::string profile =
#ifdef LIBYTDLP_DEPLOYMENT_PROFILE
        LIBYTDLP_DEPLOYMENT_PROFILE;
#else
        "unknown";
#endif
    std::string commit =
#ifdef LIBYTDLP_YTDLP_COMMIT
        LIBYTDLP_YTDLP_COMMIT;
#else
        "unknown";
#endif
    bool host_owned = Runtime::instance().host_owned();

    GilGuard guard;
    return runtime_info_json(profile, commit, LIBYTDLP_ABI_VERSION, host_owned);
}

} // namespace libytdlp
