#ifndef LIBYTDLP_BRIDGE_EMBEDDED_HPP
#define LIBYTDLP_BRIDGE_EMBEDDED_HPP

#include <Python.h>

#include <string>

namespace libytdlp {

bool boot_embedded_bridge(std::string& error);

} // namespace libytdlp

#endif
