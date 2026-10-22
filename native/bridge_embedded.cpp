#include "bridge_embedded.hpp"

#include <Python.h>

#include <string>

#include "bridge_sources.h"
#include "py_handle.hpp"

namespace libytdlp {
namespace {

bool set_child_attr(PyObject* modules, const char* parent, const char* dotted) {
    std::string child(dotted + std::string(parent).size() + 1);

    OwnedPyRef package(PyDict_GetItemString(modules, parent));
    OwnedPyRef member(PyDict_GetItemString(modules, dotted));

    if (!package || !member) {
        return false;
    }

    Py_INCREF(package.get());
    Py_INCREF(member.get());

    return PyObject_SetAttrString(package.get(), child.c_str(), member.get()) == 0;
}

} // namespace

bool boot_embedded_bridge(std::string& error) {
    OwnedPyRef modules(PyImport_GetModuleDict());
    Py_XINCREF(modules.get());

    if (!modules) {
        error = "could not read sys.modules";
        return false;
    }

    for (const BridgeSource& source : kBridgeSources) {
        OwnedPyRef module(PyModule_New(source.module));

        if (!module) {
            error = std::string("could not create module ") + source.module;
            return false;
        }

        if (PyDict_SetItemString(modules.get(), source.module, module.get()) != 0) {
            error = std::string("could not register module ") + source.module;
            return false;
        }
    }

    OwnedPyRef package(PyDict_GetItemString(modules.get(), "libytdlp_bridge"));
    Py_INCREF(package.get());

    OwnedPyRef empty_path(PyList_New(0));

    if (!package || !empty_path ||
        PyObject_SetAttrString(package.get(), "__path__", empty_path.get()) != 0) {
        error = "could not prepare bridge package";
        return false;
    }

    for (const BridgeSource& source : kBridgeSources) {
        OwnedPyRef module(PyDict_GetItemString(modules.get(), source.module));
        Py_INCREF(module.get());

        OwnedPyRef code(Py_CompileString(source.text, source.file, Py_file_input));

        if (!code) {
            error = std::string("could not compile embedded ") + source.file;
            return false;
        }

        OwnedPyRef globals(PyModule_GetDict(module.get()));
        Py_INCREF(globals.get());

        OwnedPyRef result(PyEval_EvalCode(code.get(), globals.get(), globals.get()));

        if (!result) {
            error = std::string("could not execute embedded ") + source.file;
            return false;
        }
    }

    const char* parent = "libytdlp_bridge";

    for (const BridgeSource& source : kBridgeSources) {
        std::string name(source.module);

        if (name != parent && !set_child_attr(modules.get(), parent, name.c_str())) {
            error = std::string("could not link embedded ") + name;
            return false;
        }
    }

    return true;
}

} // namespace libytdlp
