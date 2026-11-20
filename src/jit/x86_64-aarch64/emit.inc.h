/* src/jit/x86_64-aarch64/emit.inc.h - AArch64 encoders. SPDX-License-Identifier: Apache-2.0 */
/* ------------------------------------------------------------ AArch64 encoders */
static inline void E(u32 w) { *jcur++ = w; }
#define SFB(sf) ((sf) ? 0x80000000u : 0u)

static void a_movz(int sf, int rd, u32 imm, int hw) { E(SFB(sf) | 0x52800000u | (u32) hw << 21 | (imm & 0xffff) << 5 | (u32) rd); }
static void a_movk(int sf, int rd, u32 imm, int hw) { E(SFB(sf) | 0x72800000u | (u32) hw << 21 | (imm & 0xffff) << 5 | (u32) rd); }
static void a_movn(int sf, int rd, u32 imm, int hw) { E(SFB(sf) | 0x12800000u | (u32) hw << 21 | (imm & 0xffff) << 5 | (u32) rd); }
/* AArch64 logical immediate (bitmask): a rotated run of ones repeated in 2, 4, 8, 16, 32 or 64 bit elements. For sf = 0 the value is the low 32 bits.
 * opc 0 and, 1 orr, 2 eor, 3 ands. Returns 1 and emits one instruction, or 0 (nothing emitted) when v has no such form. rd 31 is sp for 0..2, zr for ands. */
static int a_logimm(int sf, int opc, int rd, int rn, u64 v) {
    if (!sf) { v &= 0xffffffffULL; v |= v << 32; }
    if (v == 0 || v == ~0ULL) return 0;
    int e = 64;
    while (e > 2) { int h = e / 2; u64 m = (1ULL << h) - 1; if ((v & m) != ((v >> h) & m)) break; e = h; }
    u64 em = e == 64 ? ~0ULL : (1ULL << e) - 1, el = v & em;
    int ones = __builtin_popcountll(el);
    u64 run = ones == 64 ? ~0ULL : (1ULL << ones) - 1;
    for (int r = 0; r < e; r++) {
        u64 rot = r == 0 ? run : (((run >> r) | (run << (e - r))) & em);       /* ror within the element */
        if (rot != el) continue;
        u32 imms = (u32)((~(2 * e - 1) | (ones - 1)) & 0x3f), N = e == 64;
        E(SFB(sf) | (u32) opc << 29 | 0x12000000u | N << 22 | (u32) r << 16 | imms << 10 | (u32) rn << 5 | (u32) rd);
        return 1;
    }
    return 0;
}
static void a_movi(int rd, u64 v);
static void a_movia(int rd, u64 v) {                      /* a guest ADDRESS: adrp + add when it lies within +-4GB of the code being emitted (2 instructions, 1 when the low 12 bits are 0), else a_movi */
    if (g_jpers) { a_movi(rd, v); return; }                /* the adrp offset depends on where the block sits: a block of the persistent cache moves */
    s64 pg = (s64) (v >> 12) - (s64) ((u64) (uintptr_t) jcur >> 12);
    if (pg >= -(1 << 20) && pg < (1 << 20) && (v >> 47) == 0) {
        u32 im = (u32) pg & 0x1fffff;
        E(0x90000000u | (im & 3) << 29 | (im >> 2) << 5 | (u32) rd);
        if (v & 0xfff) E(0x91000000u | 1u << 31 | (u32) (v & 0xfff) << 10 | (u32) rd << 5 | (u32) rd);   /* add xd, xd, #lo12 (64-bit) */
        return;
    }
    a_movi(rd, v);
}
static void a_movi(int rd, u64 v) {                       /* any 64-bit constant, 1-4 instructions, never touches the flags */
    int nz = 0, nzn = 0, first = 1;
    for (int h = 0; h < 4; h++) { nz += ((v >> (16 * h)) & 0xffff) != 0; nzn += ((~v >> (16 * h)) & 0xffff) != 0; }
    if (nzn < nz) {
        for (int h = 0; h < 4; h++) {
            u32 ch = (u32)(v >> (16 * h)) & 0xffff;
            if (ch == 0xffff) continue;
            if (first) { a_movn(1, rd, ~ch & 0xffff, h); first = 0; } else a_movk(1, rd, ch, h);
        }
        if (first) a_movn(1, rd, 0, 0);
    } else {
        for (int h = 0; h < 4; h++) {
            u32 ch = (u32)(v >> (16 * h)) & 0xffff;
            if (!ch) continue;
            if (first) { a_movz(1, rd, ch, h); first = 0; } else a_movk(1, rd, ch, h);
        }
        if (first) a_movz(1, rd, 0, 0);
    }
}
static void a_addsub(int sf, int sub, int s, int rd, int rn, int rm) {
    E(SFB(sf) | (sub ? 0x40000000u : 0) | (s ? 0x20000000u : 0) | 0x0B000000u | (u32) rm << 16 | (u32) rn << 5 | (u32) rd);
}
static void a_addsh(int rd, int rn, int rm, int lsl) {      /* add xd, xn, xm, lsl #n */
    E(0x8B000000u | (u32) rm << 16 | (u32) lsl << 10 | (u32) rn << 5 | (u32) rd);
}
static void a_addi(int sub, int rd, int rn, u32 imm12, int sh12) {    /* x-form add/sub immediate (rn, rd may be sp) */
    E(0x91000000u | (sub ? 0x40000000u : 0) | (u32) sh12 << 22 | (imm12 & 0xfff) << 10 | (u32) rn << 5 | (u32) rd);
}
static void a_log(int sf, int opc, int n, int rd, int rn, int rm) {    /* opc: 0 and, 1 orr, 2 eor, 3 ands; n: invert rm */
    E(SFB(sf) | (u32) opc << 29 | 0x0A000000u | (u32) n << 21 | (u32) rm << 16 | (u32) rn << 5 | (u32) rd);
}
static void a_logsh(int opc, int rd, int rn, int rm, int type, int amt) {   /* x-form with shifted rm */
    E(0x80000000u | (u32) opc << 29 | 0x0A000000u | (u32) type << 22 | (u32) rm << 16 | (u32) amt << 10 | (u32) rn << 5 | (u32) rd);
}
static void a_mov(int rd, int rm) { a_log(1, 1, 0, rd, 31, rm); }
static void a_movw(int rd, int rm) { a_log(0, 1, 0, rd, 31, rm); }      /* 32-bit move: zero-extends */
static void a_andmask(int rd, int rn, int nbits) { E(0x92400000u | (u32)(nbits - 1) << 10 | (u32) rn << 5 | (u32) rd); }   /* and xd, xn, #(2^nbits - 1) */
static void a_shv(int sf, int op, int rd, int rn, int rm) { E(SFB(sf) | 0x1AC02000u | (u32) op << 10 | (u32) rm << 16 | (u32) rn << 5 | (u32) rd); }   /* 0 lsl 1 lsr 2 asr 3 ror */
static void a_bfm(int sf, int sbfm, int rd, int rn, int immr, int imms) {
    E(SFB(sf) | (sbfm ? 0x13000000u : 0x53000000u) | (sf ? 0x400000u : 0) | (u32) immr << 16 | (u32) imms << 10 | (u32) rn << 5 | (u32) rd);
}
static void a_bfi(int rd, int rn, int lsb, int width) { E(0xB3400000u | (u32)((64 - lsb) & 63) << 16 | (u32)(width - 1) << 10 | (u32) rn << 5 | (u32) rd); }   /* bfi xd, xn, #lsb, #width */
static void a_extr(int sf, int rd, int rn, int rm, int lsb) { E((sf ? 0x93C00000u : 0x13800000u) | (u32) rm << 16 | (u32) lsb << 10 | (u32) rn << 5 | (u32) rd); }   /* extr rd, rn, rm, #lsb (rn = rm: ror rd, rn, #lsb) */
static void a_lsl(int sf, int rd, int rn, int n) { int b = sf ? 64 : 32; a_bfm(sf, 0, rd, rn, (b - n) & (b - 1), b - 1 - n); }
static void a_lsr(int sf, int rd, int rn, int n) { a_bfm(sf, 0, rd, rn, n, sf ? 63 : 31); }
static void a_asr(int sf, int rd, int rn, int n) { a_bfm(sf, 1, rd, rn, n, sf ? 63 : 31); }
static void a_sxtb(int sf, int rd, int rn) { a_bfm(sf, 1, rd, rn, 0, 7); }
static void a_sxth(int sf, int rd, int rn) { a_bfm(sf, 1, rd, rn, 0, 15); }
static void a_sxtw(int rd, int rn) { a_bfm(1, 1, rd, rn, 0, 31); }
static void a_mul(int sf, int rd, int rn, int rm) { E(SFB(sf) | 0x1B007C00u | (u32) rm << 16 | (u32) rn << 5 | (u32) rd); }
static void a_csel(int sf, int cc, int rd, int rn, int rm) { E(SFB(sf) | 0x1A800000u | (u32) rm << 16 | (u32) cc << 12 | (u32) rn << 5 | (u32) rd); }
static void a_mulh(int sgn, int rd, int rn, int rm) { E((sgn ? 0x9B407C00u : 0x9BC07C00u) | (u32) rm << 16 | (u32) rn << 5 | (u32) rd); }   /* smulh / umulh */
static void a_rev(int sf, int rd, int rn) { E((sf ? 0xDAC00C00u : 0x5AC00800u) | (u32) rn << 5 | (u32) rd); }   /* rev: byte swap */
static void a_rbit(int sf, int rd, int rn) { E(SFB(sf) | 0x5AC00000u | (u32) rn << 5 | (u32) rd); }   /* rbit */
static void a_clz(int sf, int rd, int rn) { E(SFB(sf) | 0x5AC01000u | (u32) rn << 5 | (u32) rd); }    /* clz */
static void a_cset(int rd, int cc) { E(0x1A9F07E0u | (u32)(cc ^ 1) << 12 | (u32) rd); }              /* cset wd, cc */
/* loads and stores: size 0 b, 1 h, 2 w, 3 x;  opc 0 store, 1 load, 2 load sign-extended to x, 3 load sign-extended to w */
static void a_ldst_u(int size, int opc, int rt, int rn, u32 off) { E((u32) size << 30 | 0x39000000u | (u32) opc << 22 | ((off >> size) & 0xfff) << 10 | (u32) rn << 5 | (u32) rt); }
static void a_ldst_s(int size, int opc, int rt, int rn, int off) { E((u32) size << 30 | 0x38000000u | (u32) opc << 22 | ((u32) off & 0x1ff) << 12 | (u32) rn << 5 | (u32) rt); }
static void a_ldst_pre(int size, int opc, int rt, int rn, int off) { E((u32) size << 30 | 0x38000C00u | (u32) opc << 22 | ((u32) off & 0x1ff) << 12 | (u32) rn << 5 | (u32) rt); }    /* str/ldr rt, [rn, #off]!  (rt != rn) */
static void a_ldst_post(int size, int opc, int rt, int rn, int off) { E((u32) size << 30 | 0x38000400u | (u32) opc << 22 | ((u32) off & 0x1ff) << 12 | (u32) rn << 5 | (u32) rt); }   /* str/ldr rt, [rn], #off   (rt != rn) */
static void a_addx(int rd, int rn, int rm, int opt, int sh) { E(0x8B200000u | (u32) rm << 16 | (u32) opt << 13 | (u32) sh << 10 | (u32) rn << 5 | (u32) rd); }   /* add xd, xn, wm/xm, <uxtb|uxth|uxtw|uxtx> #sh (opt 0..3, sh 0..4); rn, rd are not sp */
static void a_stp(int rt, int rt2, int rn, int off) { E(0xA9000000u | ((u32)(off / 8) & 0x7f) << 15 | (u32) rt2 << 10 | (u32) rn << 5 | (u32) rt); }
static void a_ldp(int rt, int rt2, int rn, int off) { E(0xA9400000u | ((u32)(off / 8) & 0x7f) << 15 | (u32) rt2 << 10 | (u32) rn << 5 | (u32) rt); }
/* persistent cache (see tcg_target.inc.h): what a block emitted for it may reference outside itself */
static inline int jpers_on(void) { return g_jpers && jt_start; }
static inline int jpers_in(const u32 *t) { return t >= jt_start && t <= jcur; }
static void jpers_rel(u32 kind, u64 val) {                    /* the word emitted next needs fixing when the block is loaded elsewhere */
    if (jnrel >= JREL_MAX) { jpers_bad = 1; return; }
    jrel[jnrel].w = (u32)(jcur - jt_start); jrel[jnrel].kind = kind; jrel[jnrel].val = val; jnrel++;
}
static void a_b(u32 *t) {
    if (jpers_on() && !jpers_in(t)) { if (t == jepi) jpers_rel(JR_JEPI, 0); else jpers_bad = 1; }
    E(0x14000000u | ((u32)(t - jcur) & 0x3ffffff));
}
static u32 *a_bcond_fwd(int cc) { E(0x54000000u | (u32) cc); return jcur - 1; }        /* patched with jpatch19 */
static u32 *a_cbz_fwd(int nz, int rt) { E((nz ? 0xB5000000u : 0xB4000000u) | (u32) rt); return jcur - 1; }
static void jpatch14(u32 *at) { *at |= ((u32)(jcur - at) & 0x3fff) << 5; }
static void jpatch19(u32 *at) { *at |= ((u32)(jcur - at) & 0x7ffff) << 5; }
static void a_adr_at(u32 *at, int rd, u32 *target) { if (jpers_on() && !jpers_in(target)) jpers_bad = 1; s64 d = (s64)((u8*) target - (u8*) at); *at = 0x10000000u | ((u32) d & 3) << 29 | (((u32)(d >> 2)) & 0x7ffff) << 5 | (u32) rd; }
static void a_bl(u32 *t) { if (jpers_on() && !jpers_in(t)) jpers_bad = 1; E(0x94000000u | ((u32)(t - jcur) & 0x3ffffff)); }
static void a_ret(void) { E(0xD65F03C0u); }
static void a_div(int sf, int sgn, int rd, int rn, int rm) { E(SFB(sf) | 0x1AC00800u | (sgn ? 0x400u : 0u) | (u32) rm << 16 | (u32) rn << 5 | (u32) rd); }       /* udiv / sdiv */
static void a_msub(int sf, int rd, int rn, int rm, int ra) { E(SFB(sf) | 0x1B008000u | (u32) rm << 16 | (u32) ra << 10 | (u32) rn << 5 | (u32) rd); }     /* rd = ra - rn * rm */
static u32 *a_tbnz_fwd(int bit, int rt) { E(0x37000000u | (u32) bit << 19 | (u32) rt); return jcur - 1; }      /* tbnz xt, #bit (< 32), patched with jpatch14 */
static u32 *a_b_fwd(void) { E(0x14000000u); return jcur - 1; }
static void jpatch26(u32 *at) { *at |= (u32)(jcur - at) & 0x3ffffff; }
static void f_cmp(int sz, int vn, int vm) { E((sz == 8 ? 0x1E602000u : 0x1E202000u) | (u32) vm << 16 | (u32) vn << 5); }          /* fcmp dn, dm */
static void f_tox(int sz, int rd, int vn) { E((sz == 8 ? 0x9E660000u : 0x1E260000u) | (u32) vn << 5 | (u32) rd); }                  /* fmov xd, dn / fmov wd, sn */
static void a_br(int rn) { E(0xD61F0000u | (u32) rn << 5); }
static void a_adr(int rd, u32 *target) { if (jpers_on() && !jpers_in(target)) jpers_bad = 1; s64 d = (s64)((u8*) target - (u8*) jcur); E(0x10000000u | ((u32) d & 3) << 29 | (((u32)(d >> 2)) & 0x7ffff) << 5 | (u32) rd); }
static int jpers_helper_ok(void *f);                            /* jcache.inc.h: f lies inside the emulator's own image */
static void j_callraw(void *f) {                                /* call a C helper; x0..x5 = arguments. Clobbers every caller-saved register: see j_callabs */
    if (jpers_on()) {                                           /* fixed length (movz + 3 movk), so the address can be rewritten when the block is loaded */
        if (!jpers_helper_ok(f)) jpers_bad = 1; else jpers_rel(JR_HELPER, (u64)(uintptr_t) f - (u64)(uintptr_t) jit_anchor);
        a_movz(1, 16, (u32)(u64)(uintptr_t) f & 0xffff, 0);
        for (int h = 1; h < 4; h++) a_movk(1, 16, (u32)((u64)(uintptr_t) f >> (16 * h)) & 0xffff, h);
    } else a_movi(16, (u64) f);
#ifdef PEMU_JIT_EMBED
    E(0xD4000001u | 0x7001u << 5);                              /* svc #0x7001: host call (handled by tests/oracle/aarch64/embed_hook.inc.h in test builds) */
#else
    E(0xD63F0000u | 16u << 5);                                  /* blr x16 */
#endif
}
static void j_sync_icache(void *a, void *b) {
#ifndef PEMU_JIT_EMBED
    __builtin___clear_cache((char*) a, (char*) b);
#else
    (void) a; (void) b;
#endif
}

