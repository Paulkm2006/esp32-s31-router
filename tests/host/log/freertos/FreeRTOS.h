/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#define portMAX_DELAY 0xffffffffU
typedef struct { bool held; } StaticSemaphore_t;
