#ifndef LIBYTDLP_BRIDGE_HPP
#define LIBYTDLP_BRIDGE_HPP

#include <libytdlp/libytdlp.h>

#include <string>

#include <Python.h>

namespace libytdlp {

struct BridgeResult {
    bool ok = false;
    std::string json;
    ly_status status = LY_E_INTERNAL;
    std::string message;
    std::string exception_class;
};

BridgeResult extract_json(const std::string& url, const std::string& options);
BridgeResult download_json(
    const std::string& url,
    const std::string& context_options,
    const std::string& job_options,
    PyObject* events,
    PyObject* cancel_event
);
BridgeResult runtime_info_json();

} // namespace libytdlp

#endif
