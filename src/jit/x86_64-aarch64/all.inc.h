/* src/jit/x86_64-aarch64/all.inc.h - manifest: JIT back end, x86_64 guest -> AArch64 host. SPDX-License-Identifier: Apache-2.0
 * Included only when HAVE_JIT is set (aarch64 host, or -DPEMU_JIT_EMBED for the x86_64 test build). Order matters: each part uses what the ones above it define.
 * Another guest/host pair gets its own sibling directory (src/jit/x86_64-aarch64/<guest>-<host>/) with its own all.inc.h. */
#include "tcg_target.inc.h"     /* overview, configuration, register map */
#include "emit.inc.h"           /* host encoders (AArch64 instruction words) */
#include "guest_state.inc.h"    /* guest register / memory access in generated code */
#include "block_cache.inc.h"    /* translated-block hash table */
#include "decode.inc.h"         /* static decoder of guest instructions (no guest state) */
#include "codegen.inc.h"        /* flag state and per-instruction code generation */
#include "jcache.inc.h"         /* persistent translation cache (PEMU_JIT_CACHE) */
#include "translate.inc.h"      /* block translation, dispatch, hooks (+ prof.inc.h in test builds) */
