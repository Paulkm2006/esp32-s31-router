/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#ifdef _MSC_VER
#define __attribute__(...)
#define strtok_r strtok_s
#endif
#include "sdkconfig.h"
#ifndef CONFIG_ROUTER_ADMIN_PASSWORD
#define CONFIG_ROUTER_ADMIN_PASSWORD "ChangeAdmin-S31!"
#endif
