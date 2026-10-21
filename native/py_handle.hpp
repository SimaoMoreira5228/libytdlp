#ifndef LIBYTDLP_PY_HANDLE_HPP
#define LIBYTDLP_PY_HANDLE_HPP

#include <Python.h>

#include <utility>

namespace libytdlp {

class OwnedPyRef {
  public:
    OwnedPyRef() : ref_(nullptr) {
    }

    explicit OwnedPyRef(PyObject* ref) : ref_(ref) {
    }

    OwnedPyRef(const OwnedPyRef&) = delete;
    OwnedPyRef& operator=(const OwnedPyRef&) = delete;

    OwnedPyRef(OwnedPyRef&& other) noexcept : ref_(std::exchange(other.ref_, nullptr)) {
    }

    OwnedPyRef& operator=(OwnedPyRef&& other) noexcept {
        if (this != &other) {
            reset(std::exchange(other.ref_, nullptr));
        }
        return *this;
    }

    ~OwnedPyRef() {
        reset(nullptr);
    }

    PyObject* get() const {
        return ref_;
    }

    PyObject* release() {
        return std::exchange(ref_, nullptr);
    }

    void reset(PyObject* ref) {
        PyObject* old = std::exchange(ref_, ref);
        Py_XDECREF(old);
    }

    explicit operator bool() const {
        return ref_ != nullptr;
    }

  private:
    PyObject* ref_;
};

class GilGuard {
  public:
    GilGuard() : state_(PyGILState_Ensure()) {
    }

    GilGuard(const GilGuard&) = delete;
    GilGuard& operator=(const GilGuard&) = delete;

    ~GilGuard() {
        PyGILState_Release(state_);
    }

  private:
    PyGILState_STATE state_;
};

} // namespace libytdlp

#endif
