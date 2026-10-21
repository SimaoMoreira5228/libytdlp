#ifndef LIBYTDLP_JOBS_HPP
#define LIBYTDLP_JOBS_HPP

#include <libytdlp/libytdlp.h>

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

#include <Python.h>

namespace libytdlp {

class DownloadJob {
  public:
    static DownloadJob* create(
        std::string url,
        std::string context_options,
        std::string job_options,
        std::string& error
    );

    DownloadJob(const DownloadJob&) = delete;
    DownloadJob& operator=(const DownloadJob&) = delete;

    ~DownloadJob();

    ly_job_state poll(std::string& events_json);
    void wait(
        std::string& json_out,
        ly_status& status_out,
        std::string& message_out,
        std::string& class_out
    );
    void cancel();

  private:
    DownloadJob(std::string url, std::string context_options, std::string job_options);

    bool arm(std::string& error);
    void run();

    mutable std::mutex mutex_;
    std::condition_variable done_;
    ly_job_state state_ = LY_JOB_RUNNING;
    std::string outcome_json_;
    ly_status outcome_status_ = LY_OK;
    std::string outcome_message_;
    std::string outcome_class_;
    std::string url_;
    std::string context_options_;
    std::string job_options_;
    PyObject* events_ = nullptr;
    PyObject* cancel_event_ = nullptr;
    std::thread worker_;
};

} // namespace libytdlp

#endif
