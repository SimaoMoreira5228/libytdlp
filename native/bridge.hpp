#ifndef LIBYTDLP_BRIDGE_HPP
#define LIBYTDLP_BRIDGE_HPP

#include <libytdlp/libytdlp.h>

#include <string>

namespace libytdlp {

struct BridgeResult {
    bool ok = false;
    std::string json;
    ly_status status = LY_E_INTERNAL;
    std::string message;
};

BridgeResult extract_json(const std::string& url, const std::string& options);

} // namespace libytdlp

#endif
