#ifndef LIBYTDLP_INSTALL_DIRS_HPP
#define LIBYTDLP_INSTALL_DIRS_HPP

#include <string>

namespace libytdlp {

std::string module_dir();
std::string staged_share_dir();
std::string staged_stdlib_dir();
std::string staged_stdlib_for_dirs(const char* bridge_dir, const char* ytdlp_dir);
bool has_staged_runtime();

} // namespace libytdlp

#endif
