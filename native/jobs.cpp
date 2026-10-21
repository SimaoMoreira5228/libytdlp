#include "jobs.hpp"

#include <Python.h>

#include <utility>

#include "bridge.hpp"
#include "py_handle.hpp"

namespace libytdlp {

DownloadJob::DownloadJob(std::string url, std::string options) :
    url_(std::move(url)), options_(std::move(options)) {
}

DownloadJob::~DownloadJob() {
    if (worker_.joinable()) {
        worker_.join();
    }

    if (events_ != nullptr || cancel_event_ != nullptr) {
        GilGuard guard;
        Py_XDECREF(events_);
        Py_XDECREF(cancel_event_);
    }
}

DownloadJob* DownloadJob::create(std::string url, std::string options, std::string& error) {
    DownloadJob* job = new (std::nothrow) DownloadJob(std::move(url), std::move(options));
    if (job == nullptr) {
        error = "could not allocate download job";
        return nullptr;
    }

    if (!job->arm(error)) {
        delete job;
        return nullptr;
    }

    return job;
}

bool DownloadJob::arm(std::string& error) {
    {
        GilGuard guard;

        OwnedPyRef events(PyList_New(0));
        if (!events) {
            PyErr_Clear();
            error = "could not allocate progress list";
            return false;
        }

        OwnedPyRef threading(PyImport_ImportModule("threading"));
        if (!threading) {
            PyErr_Clear();
            error = "could not import threading";
            return false;
        }

        OwnedPyRef factory(PyObject_GetAttrString(threading.get(), "Event"));
        if (!factory || !PyCallable_Check(factory.get())) {
            PyErr_Clear();
            error = "threading.Event is missing";
            return false;
        }

        OwnedPyRef cancel(PyObject_CallNoArgs(factory.get()));
        if (!cancel) {
            PyErr_Clear();
            error = "could not allocate cancel event";
            return false;
        }

        events_ = events.release();
        cancel_event_ = cancel.release();
    }

    try {
        worker_ = std::thread(&DownloadJob::run, this);
    } catch (const std::exception& caught) {
        GilGuard guard;
        Py_XDECREF(events_);
        Py_XDECREF(cancel_event_);
        events_ = nullptr;
        cancel_event_ = nullptr;
        error = caught.what();
        return false;
    }

    return true;
}

void DownloadJob::run() {
    BridgeResult called;

    {
        GilGuard guard;
        called = download_json(url_, options_, events_, cancel_event_);
    }

    std::lock_guard<std::mutex> lock(mutex_);

    if (called.ok) {
        state_ = LY_JOB_DONE;
        outcome_json_ = std::move(called.json);
        outcome_status_ = LY_OK;
        outcome_message_.clear();
    } else if (called.status == LY_E_CANCELLED) {
        state_ = LY_JOB_CANCELLED;
        outcome_status_ = LY_E_CANCELLED;
        outcome_message_ = std::move(called.message);
    } else {
        state_ = LY_JOB_FAILED;
        outcome_status_ = called.status;
        outcome_message_ = std::move(called.message);
    }

    done_.notify_all();
}

ly_job_state DownloadJob::poll(std::string& events_json) {
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (state_ != LY_JOB_RUNNING) {
            events_json = "[]";
            return state_;
        }
    }

    std::string joined = "[";

    {
        GilGuard guard;

        Py_ssize_t count = PyList_Size(events_);
        if (count < 0) {
            PyErr_Clear();
            count = 0;
        }

        bool first = true;

        for (Py_ssize_t index = 0; index < count; ++index) {
            PyObject* item = PyList_GetItem(events_, index);
            if (item == nullptr) {
                PyErr_Clear();
                break;
            }

            const char* text = PyUnicode_AsUTF8(item);
            if (text == nullptr) {
                PyErr_Clear();
                continue;
            }

            if (!first) {
                joined += ",";
            }

            first = false;
            joined += text;
        }

        if (PyList_SetSlice(events_, 0, count, nullptr) != 0) {
            PyErr_Clear();
        }
    }

    joined += "]";

    std::lock_guard<std::mutex> lock(mutex_);

    if (state_ != LY_JOB_RUNNING) {
        events_json = "[]";
        return state_;
    }

    events_json = std::move(joined);
    return LY_JOB_RUNNING;
}

void DownloadJob::wait(std::string& json_out, ly_status& status_out, std::string& message_out) {
    std::unique_lock<std::mutex> lock(mutex_);

    done_.wait(lock, [this] {
        return state_ != LY_JOB_RUNNING;
    });

    if (state_ == LY_JOB_DONE) {
        json_out = outcome_json_;
        status_out = LY_OK;
        message_out.clear();
        return;
    }

    json_out.clear();
    status_out = outcome_status_;
    message_out = outcome_message_;
}

void DownloadJob::cancel() {
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (state_ != LY_JOB_RUNNING) {
            return;
        }
    }

    GilGuard guard;

    OwnedPyRef ack(PyObject_CallMethod(cancel_event_, "set", nullptr));
    if (!ack) {
        PyErr_Clear();
    }
}

} // namespace libytdlp
