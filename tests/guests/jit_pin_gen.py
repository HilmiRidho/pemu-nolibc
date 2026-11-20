#!/usr/bin/env python3
# tests/guests/jit_pin_gen.py - writes tests/guests/jit_pin.c: a differential test for the guest register pinning of the AArch64 JIT (r8..r15 live in host registers).
# usage: jit_pin_gen.py [scale] > jit_pin.c          (scale 1 = about 3000 cases)
#
# Every case is one inline-asm block that loads all 15 general registers with (pseudo-random) values, loads the flags, runs ONE instruction form (or a short
# sequence), and prints every register, the flags and a hash of a scratch buffer. The run is compared natively, under the interpreter and under the JIT:
# a translated instruction that clobbers a register it does not own (every r8..r15 is live and different in every case), a pinned register that is not
# written back, a C helper call that loses a caller-saved pinned register, a wrong partial-register merge: all of them change a line.
# Flags that the SDM calls undefined are only printed on a '~' line (compared between the interpreter and the JIT, not with the native run).
import random, sys

scale = float(sys.argv[1]) if len(sys.argv) > 1 else 1.0
rng = random.Random(0x5EED64)

GP = ['rax', 'rcx', 'rdx', 'rbx', 'rsi', 'rdi', 'rbp', 'r8', 'r9', 'r10', 'r11', 'r12', 'r13', 'r14', 'r15']
EXT = GP[7:]
N32 = {'rax': 'eax', 'rcx': 'ecx', 'rdx': 'edx', 'rbx': 'ebx', 'rsi': 'esi', 'rdi': 'edi', 'rbp': 'ebp'}
N16 = {'rax': 'ax', 'rcx': 'cx', 'rdx': 'dx', 'rbx': 'bx', 'rsi': 'si', 'rdi': 'di', 'rbp': 'bp'}
N8 = {'rax': 'al', 'rcx': 'cl', 'rdx': 'dl', 'rbx': 'bl', 'rsi': 'sil', 'rdi': 'dil', 'rbp': 'bpl'}
SUF = {8: 'b', 16: 'w', 32: 'l', 64: 'q'}
CC_DEF = ['b', 'ae', 'e', 'ne', 'be', 'a', 's', 'ns', 'p', 'np']      # conditions that do not read OF (undefined after most shifts)
CC = ['o', 'no', 'b', 'ae', 'e', 'ne', 'be', 'a', 's', 'ns', 'p', 'np', 'l', 'ge', 'le', 'g']

def rn(r, sz):
    if sz == 64: return '%' + r
    if r in EXT: return '%' + r + {32: 'd', 16: 'w', 8: 'b'}[sz]
    return '%' + {32: N32, 16: N16, 8: N8}[sz][r]

def pick(excl=(), pool=None):
    pool = [r for r in (pool or GP) if r not in excl]
    return rng.choice(pool)

def pick_ext_mix(excl=()):
    """a register with a bias towards r8..r15"""
    return pick(excl, EXT if rng.random() < 0.65 else GP)

IMMS = [0, 1, 2, 3, 7, 15, 31, 63, 100, 127, -1, -2, -128, -100, 255, 256, 0x7fff, 0x8000, -32768, 0x12345, 0x7fffffff, -0x7fffffff - 1, -0x12345678]

def imm_for(sz, wide=False):
    v = rng.choice(IMMS) if rng.random() < 0.7 else rng.randint(-(1 << 31), (1 << 31) - 1)
    if sz == 8: v = ((v + 128) & 255) - 128
    elif sz == 16: v = ((v + 32768) & 65535) - 32768
    return v

class Case:
    def __init__(self, insn, pre=(), fl=0x8D5, obs=True, use_all_fl_tilde=True):
        self.insn = insn if isinstance(insn, list) else [insn]
        self.pre = list(pre)
        self.fl = fl            # flags compared with the native run (0 = none)
        self.obs = obs          # the flags are read after the instruction (pushfq); otherwise a flag-overwriting instruction follows
        self.tilde = use_all_fl_tilde

cases = []
def add(*a, **k): cases.append(Case(*a, **k))

class Mem:
    """memory operand whose address lands in the middle of the scratch buffer; pre() sets base / index accordingly"""
    def __init__(self, excl=(), pool=None, allow_index=True):
        self.base = pick(set(excl) | {'rsp'}, pool)
        self.index = None; self.scale = 1; self.idx = 0
        if allow_index and rng.random() < 0.5:
            self.index = pick(set(excl) | {self.base, 'rsp'}, pool); self.scale = rng.choice([1, 2, 4, 8]); self.idx = rng.randint(0, 6)
        self.disp = rng.choice([0, 0, 8, -8, 16, 100, -100, 255, -256, 300, 4000, -4000, 70000, -70000])
    def text(self):
        if self.index: return '%d(%%%s,%%%s,%d)' % (self.disp, self.base, self.index, self.scale)
        return '%d(%%%s)' % (self.disp, self.base)
    def pre(self):
        p = []
        if self.index: p.append('mov $%d, %%%s' % (self.idx, self.index))
        p.append('lea pin_buf+256-(%d)-(%d)(%%rip), %%%s' % (self.disp, self.idx * self.scale, self.base))
        return p
    def regs(self): return {self.base} | ({self.index} if self.index else set())

F_ALL, F_LOGIC, F_SHIFT, F_ROT, F_MUL, F_BT, F_ZF, F_NONE = 0x8D5, 0x8C5, 0xC5, 0x1, 0x801, 0x1, 0x40, 0

def N(n): return max(1, int(n * scale))

# ---- 1. two-operand ALU: reg,reg / reg,imm / reg,mem / mem,reg / mem,imm
ALU = ['add', 'or', 'adc', 'sbb', 'and', 'sub', 'xor', 'cmp', 'test']
def fl_alu(op): return F_LOGIC if op in ('and', 'or', 'xor', 'test') else F_ALL
for op in ALU:
    for sz in (8, 16, 32, 64):
        for _ in range(N(14)):
            d = pick_ext_mix(); s = pick_ext_mix() if rng.random() < 0.85 else d
            add('%s %s, %s' % (op, rn(s, sz), rn(d, sz)), fl=fl_alu(op), obs=rng.random() < 0.6)
        for _ in range(N(6)):
            d = pick_ext_mix(); v = imm_for(sz)
            add('%s $%d, %s' % (op, v, rn(d, sz)), fl=fl_alu(op), obs=rng.random() < 0.6)
        for _ in range(N(8)):
            m = Mem(pool=EXT if rng.random() < 0.7 else GP); d = pick_ext_mix(m.regs())
            add('%s %s, %s' % (op, m.text(), rn(d, sz)), pre=m.pre(), fl=fl_alu(op), obs=rng.random() < 0.6)
        for _ in range(N(8)):
            m = Mem(pool=EXT if rng.random() < 0.7 else GP); s = pick_ext_mix(m.regs())
            add('%s %s, %s' % (op, rn(s, sz), m.text()), pre=m.pre(), fl=fl_alu(op), obs=rng.random() < 0.6)
        for _ in range(N(3)):
            m = Mem(); v = imm_for(sz)
            add('%s%s $%d, %s' % (op, SUF[sz], v, m.text()), pre=m.pre(), fl=fl_alu(op))

# ---- 2. compare / test + jcc, setcc, cmovcc (fused producers, host flags) and the same after a flag-neutral instruction
for cc in CC:
    for _ in range(N(6)):
        op = rng.choice(['cmp', 'test', 'sub', 'add', 'and']); sz = rng.choice([8, 16, 32, 64])
        a = pick_ext_mix(); b = pick_ext_mix(); c = pick_ext_mix()
        add(['%s %s, %s' % (op, rn(b, sz), rn(a, sz)), 'j%s 9f' % cc, 'add $%d, %s' % (rng.randint(1, 99), rn(c, 64)), '9:'], fl=0, obs=False)
        add(['%s %s, %s' % (op, rn(b, sz), rn(a, sz)), 'j%s 9f' % cc, 'add $%d, %s' % (rng.randint(1, 99), rn(c, 64)), '9:'], fl=fl_alu(op), obs=True)
    for _ in range(N(6)):
        sz = rng.choice([32, 64, 16]); a = pick_ext_mix(); b = pick_ext_mix()
        add(['cmp %s, %s' % (rn(b, sz), rn(a, sz)), 'cmov%s %s, %s' % (cc, rn(pick_ext_mix(), sz), rn(pick_ext_mix(), sz))], fl=F_ALL)
        add(['cmp %s, %s' % (rn(b, sz), rn(a, sz)), 'set%s %s' % (cc, rn(pick_ext_mix(), 8))], fl=F_ALL)
    for _ in range(N(4)):                                      # flags straight from the primer, no producer in the block
        sz = rng.choice([32, 64, 16]); m = Mem()
        add('cmov%s %s, %s' % (cc, rn(pick_ext_mix(), sz), rn(pick_ext_mix(), sz)))
        add('set%s %s' % (cc, rn(pick_ext_mix(), 8)))
        add('set%s %s' % (cc, m.text()), pre=m.pre())
        d = pick_ext_mix(m.regs())
        add('cmov%s %s, %s' % (cc, m.text(), rn(d, sz)), pre=m.pre())
        add(['jmp 8f', '7: add $3, %s' % rn(pick_ext_mix(), 64), 'jmp 9f', '8: j%s 7b' % cc, '9:'])

# ---- 3. unary, mov family, lea, extensions
for op in ('inc', 'dec', 'neg', 'not'):
    for sz in (8, 16, 32, 64):
        for _ in range(N(5)):
            add('%s %s' % (op, rn(pick_ext_mix(), sz)), fl=F_ALL, obs=rng.random() < 0.6)
        m = Mem(); add('%s%s %s' % (op, SUF[sz], m.text()), pre=m.pre(), fl=F_ALL)
for sz in (8, 16, 32, 64):
    for _ in range(N(10)):
        d = pick_ext_mix(); s = pick_ext_mix()
        add('mov %s, %s' % (rn(s, sz), rn(d, sz)))
        add('mov $%d, %s' % (imm_for(sz), rn(d, sz)))
        m = Mem(); add('mov %s, %s' % (m.text(), rn(d, sz)), pre=m.pre()); m = Mem(); add('mov %s, %s' % (rn(s, sz), m.text()), pre=m.pre())
        m = Mem(); add('mov%s $%d, %s' % (SUF[sz], imm_for(sz), m.text()), pre=m.pre())
for _ in range(N(60)):
    sz = rng.choice([32, 64, 16]); m = Mem(allow_index=rng.random() < 0.8, pool=EXT if rng.random() < 0.7 else GP)
    add('lea %s, %s' % (m.text(), rn(pick_ext_mix(), sz)), pre=m.pre())
for _ in range(N(20)):
    add('lea %d(%%%s), %s' % (rng.randint(-300, 300), pick(['rsp']) if False else pick_ext_mix(), rn(pick_ext_mix(), 64)))
for src, dst, mn in ((8, 32, 'movzbl'), (8, 64, 'movzbq'), (16, 32, 'movzwl'), (16, 64, 'movzwq'), (8, 16, 'movzbw'),
                     (8, 32, 'movsbl'), (8, 64, 'movsbq'), (16, 32, 'movswl'), (16, 64, 'movswq'), (32, 64, 'movslq'), (8, 16, 'movsbw')):
    for _ in range(N(8)):
        add('%s %s, %s' % (mn, rn(pick_ext_mix(), src), rn(pick_ext_mix(), dst)))
        m = Mem(); add('%s %s, %s' % (mn, m.text(), rn(pick_ext_mix(m.regs()), dst)), pre=m.pre())
for sz in (32, 64):
    for _ in range(N(10)): add('bswap %s' % rn(pick_ext_mix(), sz))
for op in ('cltq', 'cwtl', 'cltd', 'cqto', 'cwtd'): add(op)
for _ in range(N(10)): add('xchg %s, %%rax' % rn(pick_ext_mix(), 64)); add('xchg %s, %%eax' % rn(pick_ext_mix(), 32))

# ---- 4. legacy high-byte registers (no REX): ah ch dh bh with al cl dl bl
HI = ['%al', '%cl', '%dl', '%bl', '%ah', '%ch', '%dh', '%bh']
for op in ('mov', 'add', 'sub', 'xor', 'cmp', 'xchg', 'adc', 'and'):
    for _ in range(N(10)):
        a = rng.choice(HI); b = rng.choice(HI)
        add('%s %s, %s' % (op, a, b), fl=fl_alu(op))
for _ in range(N(8)):
    add('movzbl %s, %s' % (rng.choice(HI), rn(pick(pool=GP[:4]), 32))); add('movsbl %s, %s' % (rng.choice(HI), rn(pick(pool=GP[:4]), 32)))
    add('mov $%d, %s' % (imm_for(8), rng.choice(HI)))

# ---- 5. shifts and rotates (the count is fixed in `pre`, so cl is never a surprise)
for op in ('shl', 'shr', 'sar', 'rol', 'ror'):
    rot = op in ('rol', 'ror'); fl = F_ROT if rot else F_SHIFT
    for sz in (8, 16, 32, 64):
        for _ in range(N(5)):
            c = rng.randint(1, sz - 1); r = pick_ext_mix()
            add('%s $%d, %s' % (op, c, rn(r, sz)), fl=fl, obs=rng.random() < 0.6)
            add('%s %s' % (op, rn(pick_ext_mix(), sz)), fl=fl)                                  # by one
            r = pick_ext_mix({'rcx'})
            add('%s %%cl, %s' % (op, rn(r, sz)), pre=['mov $%d, %%ecx' % rng.randint(1, sz - 1)], fl=fl, obs=rng.random() < 0.6)
        m = Mem(excl={'rcx'}); c = rng.randint(1, sz - 1)
        add('%s%s $%d, %s' % (op, SUF[sz], c, m.text()), pre=m.pre(), fl=fl)

# ---- 6. multiply and divide
for sz in (16, 32, 64):
    for _ in range(N(14)):
        add('imul %s, %s' % (rn(pick_ext_mix(), sz), rn(pick_ext_mix(), sz)), fl=F_MUL)
        add('imul $%d, %s, %s' % (imm_for(sz), rn(pick_ext_mix(), sz), rn(pick_ext_mix(), sz)), fl=F_MUL, obs=rng.random() < 0.5)
        m = Mem(); add('imul %s, %s' % (m.text(), rn(pick_ext_mix(m.regs()), sz)), pre=m.pre(), fl=F_MUL)
for sz in (8, 16, 32, 64):
    for op in ('mul', 'imul'):
        for _ in range(N(6)):
            r = pick_ext_mix({'rax', 'rdx'}); m = Mem(excl={'rax', 'rdx'})
            add('%s %s' % (op, rn(r, sz)), fl=F_MUL, obs=rng.random() < 0.5)
            add('%s%s %s' % (op, SUF[sz], m.text()), pre=m.pre(), fl=F_MUL)
    for op in ('div', 'idiv'):
        for _ in range(N(6)):
            r = pick_ext_mix({'rax', 'rdx'}); k = rng.choice([1, 2, 3, 7, 10, 100, 12345, 0x7fff, 3000000])
            if sz == 8: k = rng.choice([1, 2, 3, 7, 10, 100])
            if op == 'div': setup = ['mov $0, %edx'] if sz > 8 else ['mov $0, %ah']
            else: setup = {8: ['cbtw'], 16: ['cwtd'], 32: ['cltd'], 64: ['cqto']}[sz]
            if sz == 8 and op == 'div': add('%s %s' % (op, rn(r, sz)), pre=['mov $%d, %s' % (k, rn(r, 32)), 'mov $0, %ah'], fl=0)
            else: add('%s %s' % (op, rn(r, sz)), pre=['mov $%d, %s' % (k, rn(r, 32))] + setup, fl=0)

# ---- 7. atomics and exchanges
for sz in (8, 16, 32, 64):
    for _ in range(N(6)):
        m = Mem(); r = pick_ext_mix(m.regs() | {'rax'})
        add('xchg %s, %s' % (rn(r, sz), m.text()), pre=m.pre())
        m = Mem(); r = pick_ext_mix(m.regs() | {'rax'}); add('lock xadd %s, %s' % (rn(r, sz), m.text()), pre=m.pre(), fl=F_ALL)
        m = Mem(); r = pick_ext_mix(m.regs() | {'rax'}); add('cmpxchg %s, %s' % (rn(r, sz), m.text()), pre=m.pre(), fl=F_ALL)
        m = Mem(); r = pick_ext_mix(m.regs() | {'rax'}); add('lock cmpxchg %s, %s' % (rn(r, sz), m.text()), pre=m.pre(), fl=F_ALL)
        m = Mem(); r = pick_ext_mix(m.regs()); add('lock %s %s, %s' % (rng.choice(['add', 'sub', 'and', 'or', 'xor']), rn(r, sz), m.text()), pre=m.pre(), fl=F_ALL if True else 0)
        m = Mem(); add('lock %s%s %s' % (rng.choice(['inc', 'dec']), SUF[sz], m.text()), pre=m.pre(), fl=F_ALL)
        d = pick_ext_mix({'rax'}); s = pick_ext_mix({'rax'}); add('cmpxchg %s, %s' % (rn(s, sz), rn(d, sz)), fl=F_ALL)
        d = pick_ext_mix(); s = pick_ext_mix(); add('xadd %s, %s' % (rn(s, sz), rn(d, sz)), fl=F_ALL)
        d = pick_ext_mix(); s = pick_ext_mix(); add('xchg %s, %s' % (rn(s, sz), rn(d, sz)))
    for _ in range(N(4)):                                                  # adc / sbb in a chain: the carry travels through the registers
        a, b, c = pick_ext_mix(), pick_ext_mix(), pick_ext_mix()
        add(['add %s, %s' % (rn(b, sz), rn(a, sz)), 'adc %s, %s' % (rn(c, sz), rn(b, sz)), 'sbb %s, %s' % (rn(a, sz), rn(c, sz))], fl=F_ALL)

# ---- 8. stack
for _ in range(N(30)):
    a, b = pick_ext_mix(), pick_ext_mix(); m = Mem()
    add(['push %%%s' % a, 'pop %%%s' % b])
    add(['push $%d' % imm_for(32), 'pop %%%s' % b])
    add(['push %s' % m.text(), 'pop %%%s' % b], pre=m.pre())
    m = Mem(); add(['push %%%s' % a, 'pop %s' % m.text()], pre=m.pre())
    c = pick_ext_mix()
    add(['push %%%s' % a, 'push %%%s' % b, 'push %%%s' % c, 'pop %%%s' % a, 'pop %%%s' % b, 'pop %%%s' % c])
for _ in range(N(30)):                                                      # indirect call / jump through a pinned register, ret
    t = pick_ext_mix({'rsp'}); c = pick_ext_mix({t})
    add(['lea 2f(%%rip), %%%s' % t, 'call *%%%s' % t, 'jmp 3f', '2: add $7, %%%s' % c, 'ret', '3:'], fl=0, obs=False)
    t = pick_ext_mix(); c = pick_ext_mix({t})
    add(['lea 2f(%%rip), %%%s' % t, 'jmp *%%%s' % t, 'add $1, %%%s' % c, '2: add $9, %%%s' % c], fl=F_ALL)
    m = Mem(); c = pick_ext_mix(m.regs())
    add(['lea 2f(%rip), %rax', 'mov %%rax, %s' % m.text(), 'call *%s' % m.text(), 'jmp 3f', '2: add $5, %%%s' % c, 'ret', '3:'], pre=m.pre(), fl=0, obs=False) if 'rax' not in m.regs() else None

# ---- 9. bit operations, scans, popcnt
for op in ('bt', 'bts', 'btr', 'btc'):
    for sz in (16, 32, 64):
        for _ in range(N(6)):
            a = pick_ext_mix(); b = pick_ext_mix({a})
            add('%s %s, %s' % (op, rn(b, sz), rn(a, sz)), fl=F_BT, obs=rng.random() < 0.7)
            add('%s $%d, %s' % (op, rng.randint(0, sz - 1), rn(a, sz)), fl=F_BT)
            m = Mem(); b = pick_ext_mix(m.regs())
            add('%s %s, %s' % (op, rn(b, sz), m.text()), pre=m.pre() + ['mov $%d, %s' % (rng.randint(-200, 200), rn(b, 64))], fl=F_BT)
            m = Mem(); add('%s%s $%d, %s' % (op, SUF[sz], rng.randint(0, sz - 1), m.text()), pre=m.pre(), fl=F_BT)
for op, fl in (('bsf', F_ZF), ('bsr', F_ZF), ('popcnt', F_ALL)):
    for sz in (16, 32, 64):
        for _ in range(N(8)):
            a, b = pick_ext_mix(), pick_ext_mix()
            nt = op != 'popcnt'                                                     # bsf / bsr: only ZF is defined, the JIT and the interpreter differ in the rest
            add('%s %s, %s' % (op, rn(b, sz), rn(a, sz)), fl=fl, use_all_fl_tilde=not nt)
            add(['xor %s, %s' % (rn(b, 32), rn(b, 32)), '%s %s, %s' % (op, rn(b, sz), rn(a, sz))], fl=fl, use_all_fl_tilde=not nt) if op != 'popcnt' else None   # a zero source
            m = Mem(); add('%s %s, %s' % (op, m.text(), rn(pick_ext_mix(m.regs()), sz)), pre=m.pre(), fl=fl, use_all_fl_tilde=not nt)

# ---- 10. instructions the translator hands to the interpreter, and flag consumers after them (jh_cc / jh_cf run while every register is live)
for op in ('shld', 'shrd'):
    for sz in (32, 64):                                           # (16-bit shld/shrd results differ from real hardware in the interpreter: not a JIT matter)
        for _ in range(N(6)):
            a, b = pick_ext_mix(), pick_ext_mix(); c = rng.randint(1, sz - 1)
            add('%s $%d, %s, %s' % (op, c, rn(b, sz), rn(a, sz)), fl=0)
            cc = rng.choice(CC_DEF)
            if sz == 16: continue                                  # the flags of a 16-bit shld/shrd differ between CPUs
            add(['%s $%d, %s, %s' % (op, c, rn(b, sz), rn(a, sz)), 'set%s %s' % (cc, rn(pick_ext_mix(), 8))], fl=0)
            add(['%s $%d, %s, %s' % (op, c, rn(b, sz), rn(a, sz)), 'cmov%s %s, %s' % (cc, rn(pick_ext_mix(), 64), rn(pick_ext_mix(), 64))], fl=0)
            add(['%s $%d, %s, %s' % (op, c, rn(b, sz), rn(a, sz)), 'adc %s, %s' % (rn(pick_ext_mix(), 64), rn(pick_ext_mix(), 64))], fl=0)
            d = pick_ext_mix(); e = pick_ext_mix()
            add(['%s $%d, %s, %s' % (op, c, rn(b, sz), rn(a, sz)), 'j%s 9f' % cc, 'add $5, %s' % rn(d, 64), '9: add $1, %s' % rn(e, 64)], fl=0, obs=False)
            add(['%s $%d, %s, %s' % (op, c, rn(b, sz), rn(a, sz)), 'inc %s' % rn(pick_ext_mix(), 64)], fl=0)
for _ in range(N(10)):
    add(['lahf', 'xchg %ah, %al' if False else 'nop'], fl=F_ALL); add(['sahf'], fl=F_ALL); add(['cmc'], fl=F_ALL); add(['stc', 'adc %s, %s' % (rn(pick_ext_mix(), 64), rn(pick_ext_mix(), 64))], fl=F_ALL)
    add(['clc', 'sbb %s, %s' % (rn(pick_ext_mix(), 32), rn(pick_ext_mix(), 32))], fl=F_ALL)

# ---- 11. SSE <-> general registers (movq/movd, cvtsi2sd, cvttsd2si through a C helper, pmovmskb)
for _ in range(N(25)):
    a, b = pick_ext_mix(), pick_ext_mix()
    add(['movq %s, %%xmm0' % rn(a, 64), 'movq %%xmm0, %s' % rn(b, 64)])
    add(['movd %s, %%xmm1' % rn(a, 32), 'movd %%xmm1, %s' % rn(b, 32)])
    add(['cvtsi2sd %s, %%xmm0' % rn(a, 64), 'cvttsd2si %%xmm0, %s' % rn(b, 64)])
    add(['cvtsi2sd %s, %%xmm0' % rn(a, 32), 'cvttsd2si %%xmm0, %s' % rn(b, 32)])
    add(['cvtsi2ss %s, %%xmm0' % rn(a, 64), 'cvttss2si %%xmm0, %s' % rn(b, 64)])
    add(['movq %s, %%xmm2' % rn(a, 64), 'pmovmskb %%xmm2, %s' % rn(b, 32)])
    add(['movq %s, %%xmm2' % rn(a, 64), 'punpcklqdq %xmm2, %xmm2', 'cvttsd2si %%xmm2, %s' % rn(b, 32), 'pmovmskb %%xmm2, %s' % rn(pick_ext_mix(), 64)])

# ---- 12. random straight-line programs: many registers live at once, flags shared between neighbours (fusing, lazy flags, liveness)
OPS2 = ['add', 'sub', 'and', 'or', 'xor', 'adc', 'sbb', 'cmp', 'test', 'mov']
def rand_insn():
    k = rng.random(); sz = rng.choice([8, 16, 32, 64, 64, 32]); s2 = rng.choice([32, 64]); P = pick_ext_mix
    if k < 0.40: return '%s %s, %s' % (rng.choice(OPS2), rn(P(), sz), rn(P(), sz))
    if k < 0.52: return '%s $%d, %s' % (rng.choice(OPS2[:-1]), imm_for(sz), rn(P(), sz))
    if k < 0.60: return '%s %s' % (rng.choice(['inc', 'dec', 'neg', 'not']), rn(P(), sz))
    if k < 0.66: return 'lea %d(%%%s,%%%s,%d), %s' % (rng.randint(-50, 50), P(), P(), rng.choice([1, 2, 4, 8]), rn(P(), s2))
    if k < 0.74: return 'cmov%s %s, %s' % (rng.choice(CC), rn(P(), s2), rn(P(), s2))
    if k < 0.80: return 'set%s %s' % (rng.choice(CC), rn(P(), 8))
    if k < 0.86:
        return 'movz%sl %s, %s' % ('b', rn(P(), 8), rn(P(), 32)) if rng.random() < 0.5 else 'movslq %s, %s' % (rn(P(), 32), rn(P(), 64))
    if k < 0.90: return 'xchg %s, %s' % (rn(P(), sz), rn(P(), sz))
    return 'shr $%d, %s' % (rng.randint(1, 7), rn(P(), s2)) if False else 'sub %s, %s' % (rn(P(), s2), rn(P(), s2))   # (shl / imul leave flags undefined: they are covered above)
for _ in range(N(260)):
    seq = [rand_insn() for _ in range(rng.randint(4, 14))]
    if rng.random() < 0.3:                                                  # a conditional branch in the middle
        i = rng.randint(1, len(seq) - 1); seq.insert(i, 'j%s 9f' % rng.choice(CC)); seq.append('9:')
    add(seq, fl=0x8C5, obs=rng.random() < 0.75)
    cases[-1].tilde = False

# ------------------------------------------------------------------ output
out = []
w = out.append
w('/* tests/guests/jit_pin.c - generated by tests/guests/jit_pin_gen.py (do not edit): %d cases */' % len(cases))
w('#include <stdio.h>\n#include <string.h>\ntypedef unsigned long long u64;')
w('u64 pin_init[16], pin_infl, pin_out[16], pin_outfl, pin_sp0, pin_sp1; unsigned char pin_buf[512] __attribute__((aligned(64)));')
w('static u64 sm(u64 *s) { u64 z = (*s += 0x9E3779B97F4A7C15ULL); z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL; z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL; return z ^ (z >> 31); }')
w('static const u64 sp_[] = {0, 1, 2, 0xff, 0x80, 0x7f, 0x7fff, 0x8000, 0xffff, 0x7fffffff, 0x80000000ULL, 0xffffffffULL, ~0ULL, 0x8000000000000000ULL, 0x7fffffffffffffffULL, 0x100000000ULL};')
CLOB = ','.join('"%s"' % r for r in GP if r != 'rsp') + ',"memory","cc","xmm0","xmm1","xmm2"'
regs = [r for r in GP]
for i, c in enumerate(cases):
    lines = []
    lines.append('lea -128(%rsp), %rsp')
    lines.append('mov %rsp, pin_sp0(%rip)')
    for k, r in enumerate(regs): lines.append('mov pin_init+%d(%%rip), %%%s' % (8 * k, r))
    lines.append('pushq pin_infl(%rip)'); lines.append('popfq')
    lines += c.pre + c.insn
    if c.obs: lines += ['pushfq', 'popq pin_outfl(%rip)']
    else: lines += ['cmp %eax, %eax']
    lines.append('mov %rsp, pin_sp1(%rip)')
    for k, r in enumerate(regs): lines.append('mov %%%s, pin_out+%d(%%rip)' % (r, 8 * k))
    lines.append('lea 128(%rsp), %rsp')
    body = '\\n\\t'.join(l.replace('\\', '\\\\').replace('"', '\\"').replace('%', '%%') for l in lines)
    w('static void c%d(void) { __asm__ volatile ("%s" ::: %s); }' % (i, body, CLOB))
w('typedef struct { void (*f)(void); unsigned fl; unsigned obs; unsigned tilde; } Case;')
w('static const Case cases[] = {')
for i, c in enumerate(cases): w(' { c%d, 0x%x, %d, %d },' % (i, c.fl, 1 if c.obs else 0, 1 if c.tilde else 0))
w('};')
w('''int main(void) {
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        u64 s = 0x1234567ULL * (i + 1);
        for (int k = 0; k < 16; k++) { u64 v = sm(&s); pin_init[k] = (v & 7) < 2 ? sp_[(v >> 8) & 15] : (v & 7) == 2 ? (v >> 40) & 0xff : v; }
        pin_infl = (sm(&s) & 0x8D5) | 2;
        for (int k = 0; k < 512; k += 8) { u64 v = sm(&s); memcpy(pin_buf + k, &v, 8); }
        pin_outfl = 0;
        cases[i].f();
        u64 h = 1469598103934665603ULL;
        for (int k = 0; k < 512; k++) h = (h ^ pin_buf[k]) * 1099511628211ULL;
        printf("%u", i);
        for (int k = 0; k < 15; k++) printf(" %llx", pin_out[k]);
        printf(" sp=%lld f=%llx b=%llx\\n", (long long) (pin_sp1 - pin_sp0), cases[i].obs ? pin_outfl & cases[i].fl : 0ULL, h);
        if (cases[i].tilde && cases[i].obs) printf("~%u f=%llx\\n", i, pin_outfl & 0x8D5);
    }
    return 0;
}''')
print('\n'.join(out))
