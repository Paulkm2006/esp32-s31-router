/* SPDX-License-Identifier: Apache-2.0 */
#include <errno.h>
#include <stddef.h>
#include <string.h>

int router_smb_gethostname(char *name, size_t size)
{
    static const char hostname[] = "esp32-router";
    if (size < sizeof(hostname)) { errno = ENAMETOOLONG; return -1; }
    memcpy(name, hostname, sizeof(hostname));
    return 0;
}
