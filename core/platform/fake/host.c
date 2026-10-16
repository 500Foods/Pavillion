#include "pav_platform.h"

void pav_platform_install_host(void) {
    pav_platform_install(pav_platform_fake());
}
