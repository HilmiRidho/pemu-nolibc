#!/usr/bin/env python3
# tests/guests/jit_inplace_gen.py - writes a C guest for the in-place translations of the AArch64 JIT: shl/shr/sar/rol/ror by an immediate and imul r, r/m[, imm]
# on a pinned register with the flags dead (one host instruction on the register itself). Every case puts a value in a chosen register, runs ONE instruction,
# kills the flags with a following cmp, and prints the register. Native, interpreter and JIT must agree line by line (tests/integration/jit_embed.sh).
# usage: jit_inplace_gen.py > jit_inplace.c
REGS = ['rax', 'rcx', 'rdx', 'rbx', 'rsi', 'rdi', 'rbp', 'r8', 'r9', 'r10', 'r11', 'r12', 'r13', 'r14', 'r15']
VALS = ['0x0', '0x1', '0x80000000UL', '0xffffffffffffffffUL', '0x123456789abcdef0UL', '0xdeadbeefcafebabeUL', '0x7fffffffUL', '0x8000000000000000UL']
COUNTS = [0, 1, 2, 3, 7, 8, 13, 15, 16, 31, 32, 33, 62, 63, 64, 65]
OPS = ['shl', 'shr', 'sar', 'rol', 'ror']
out = ['#include <stdio.h>', '#include <stdint.h>', 'static uint64_t mem[4] = { 0x0123456789abcdefULL, 0xfedcba9876543210ULL, 3, 0x8000000000000001ULL };', 'int main(void) {', '  uint64_t cnt = 0;']
def n32(r):
    return '%k0'
for r in REGS:
    for op in OPS:
        for sz in (32, 64):
            for n in COUNTS:
                for v in VALS:
                    suf, reg = ('l', '%k0') if sz == 32 else ('q', '%0')
                    out.append('  { register uint64_t x asm("%s") = %s; asm volatile("%s%s $%d, %s; cmp %%%%rsp, %%%%rsp" : "+r"(x) : : "cc"); printf("%%016lx\\n", (unsigned long) x); }' % (r, v, op, suf, n, reg))
SRC = ['rbx', 'r9', 'r13', 'r15']
DST = ['rax', 'rcx', 'rdx', 'rsi', 'rdi', 'r8', 'r10', 'r12', 'r14']
for d in DST:
    for sz in (32, 64):
        suf, a, b = ('l', '%k0', '%k1') if sz == 32 else ('q', '%0', '%1')
        for s in SRC:
            for v1 in VALS:
                for v2 in VALS[2:6]:
                    out.append('  { register uint64_t x asm("%s") = %s; register uint64_t y asm("%s") = %s; asm volatile("imul%s %s, %s; cmp %%%%rsp, %%%%rsp" : "+r"(x) : "r"(y) : "cc"); printf("%%016lx\\n", (unsigned long) x); }' % (d, v1, s, v2, suf, b, a))
        out.append('  { register uint64_t x asm("%s") = %s; asm volatile("imul%s %s, %s; cmp %%%%rsp, %%%%rsp" : "+r"(x) : : "cc"); printf("%%016lx\\n", (unsigned long) x); }' % (d, VALS[5], suf, a, a))
        for i in range(4):
            for v1 in VALS:
                out.append('  { register uint64_t x asm("%s") = %s; asm volatile("imul%s %d(%%1), %s; cmp %%%%rsp, %%%%rsp" : "+r"(x) : "r"((char*) mem) : "cc", "memory"); printf("%%016lx\\n", (unsigned long) x); }' % (d, v1, suf, i * 8, a))
        for imm in ('3', '-5', '127', '-128', '1000', '-70000', '0x7fffffff'):
            for s in SRC[:2]:
                for v1 in VALS[:6]:
                    out.append('  { register uint64_t x asm("%s") = 0x5555555555555555UL; register uint64_t y asm("%s") = %s; asm volatile("imul%s $%s, %s, %s; cmp %%%%rsp, %%%%rsp" : "+r"(x) : "r"(y) : "cc"); printf("%%016lx\\n", (unsigned long) x); }' % (d, s, v1, suf, imm, b, a))
out.append('  (void) cnt; return 0; }')
print('\n'.join(out))
