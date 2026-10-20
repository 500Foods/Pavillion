#include "platform_internal.h"
#include "pav_platform.h"

/* macOS and Windows until G-6: the fake table, with the real SRV lookup
   (G Lock). Tests install pav_platform_fake() itself and keep its table. */
void pav_platform_install_host(void) {
    static PavPlatform host;

    host = *pav_platform_fake();
    host.srv_lookup = pav_os_srv_lookup;
    pav_platform_install(&host);
}
