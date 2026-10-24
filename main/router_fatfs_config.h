/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "ffconf.h"
// A 255-character FAT long name can require 765 UTF-8 bytes.
#undef FF_LFN_BUF
#define FF_LFN_BUF (FF_MAX_LFN * 3)
