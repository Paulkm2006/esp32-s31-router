/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <assert.h>
#include "FreeRTOS.h"
typedef StaticSemaphore_t *SemaphoreHandle_t;
static inline SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *storage)
{
    storage->held = false;
    return storage;
}
static inline void xSemaphoreTake(SemaphoreHandle_t mutex, unsigned timeout)
{
    (void)timeout;
    assert(!mutex->held);
    mutex->held = true;
}
static inline void xSemaphoreGive(SemaphoreHandle_t mutex)
{
    assert(mutex->held);
    mutex->held = false;
}
