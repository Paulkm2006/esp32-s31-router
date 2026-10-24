/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

static inline bool router_subnets_overlap(uint32_t ip_a, uint32_t mask_a, uint32_t ip_b, uint32_t mask_b)
{
    return ((ip_a ^ ip_b) & mask_a & mask_b) == 0;
}
