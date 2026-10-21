#include <cstdint>

#include <libytdlp/libytdlp.h>
#include <libytdlp/version.h>

uint32_t ly_header_check_cxx() {
    return ly_abi_version();
}
