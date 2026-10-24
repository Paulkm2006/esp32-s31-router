/* SPDX-License-Identifier: Apache-2.0 */
#include "../main/network/router_subnet.h"

#define CHECK(condition) do { if (!(condition)) { return __LINE__; } } while (0)

static uint32_t swap_bytes(uint32_t value)
{
    return (value << 24) | ((value & 0x0000ff00U) << 8) |
           ((value & 0x00ff0000U) >> 8) | (value >> 24);
}

int main(void)
{
    CHECK(router_subnets_overlap(0xc0a80401U, 0xffffff00U, 0xc0a804f0U, 0xffffff00U));
    CHECK(!router_subnets_overlap(0xc0a80401U, 0xffffff00U, 0xc0a80501U, 0xffffff00U));
    CHECK(router_subnets_overlap(0xc0a80101U, 0xffff0000U, 0xc0a80401U, 0xffffff00U));
    CHECK(router_subnets_overlap(0x0a000001U, 0xff000000U, 0x0a2a0001U, 0xffffff00U));
    CHECK(!router_subnets_overlap(0x0a000001U, 0xff000000U, 0xac140001U, 0xffffff00U));
    CHECK(router_subnets_overlap(0x01020304U, 0, 0xac140001U, 0xffffff00U));
    CHECK(router_subnets_overlap(0xc0a804f0U, 0xffffffffU, 0xc0a80401U, 0xffffff00U));
    CHECK(!router_subnets_overlap(0xc0a805f0U, 0xffffffffU, 0xc0a80401U, 0xffffff00U));
    CHECK(router_subnets_overlap(swap_bytes(0xc0a80101U), swap_bytes(0xffff0000U),
                                 swap_bytes(0xc0a80401U), swap_bytes(0xffffff00U)));
    CHECK(!router_subnets_overlap(swap_bytes(0x0a000001U), swap_bytes(0xff000000U),
                                  swap_bytes(0xac140001U), swap_bytes(0xffffff00U)));
    return 0;
}
