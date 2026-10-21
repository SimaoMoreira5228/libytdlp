#ifndef LIBYTDLP_RUNTIME_HPP
#define LIBYTDLP_RUNTIME_HPP

#include <mutex>
#include <string>

namespace libytdlp {

class Runtime {
  public:
    static Runtime& instance();

    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;

    bool ensure(const char* bridge_dir, const char* ytdlp_dir, std::string& error);
    std::mutex& serial();

  private:
    Runtime() = default;

    std::mutex mutex_;
    std::mutex serial_;
    bool ready_ = false;
};

} // namespace libytdlp

#endif
