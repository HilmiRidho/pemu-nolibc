/* src/guest/x86_64/cpu/all.inc.h - manifest: the x86_64 guest CPU core (interpreter). SPDX-License-Identifier: Apache-2.0
 * One place that lists the parts and their order (each part uses what the ones above it define; the whole is one translation unit).
 * Used by src/pemu.c and by tests/unit/check_core.c, so the core cannot be built from two different lists.
 * The core must not depend on the OS layer (guest/x86_64/linux/) or on src/proot/: whatever it needs from there goes through the PemuOS table in cpu.inc.h. */
#include "cpu.inc.h"
#include "flags_mem.inc.h"
#include "decode.inc.h"
#include "sse2.inc.h"
#include "mmx.inc.h"
#include "ssse3_sse4.inc.h"
#include "x87.inc.h"
#include "interp.inc.h"
