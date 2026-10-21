#include <libytdlp/libytdlp.h>

#include <stdint.h>

uint32_t ly_header_check_c(void) {
    return ly_abi_version();
}
