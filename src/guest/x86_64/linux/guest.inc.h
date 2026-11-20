/* src/guest/x86_64/linux/guest.inc.h - what is specific to an x86_64 guest in process set-up and ELF loading. SPDX-License-Identifier: Apache-2.0
 * Used by src/os/linux/elfload.inc.h and src/os/linux/main.inc.h; a new guest provides the same names. */
#define PEMU_ELF_MACHINE EM_X86_64
#define PEMU_ELF_NAME "x86_64"
#ifdef HAVE_JIT
#define PEMU_ELF_NOTE_RWX(s, len) jit_note_rwx((s), (len))      /* never translated: x86 code may modify itself */
#else
#define PEMU_ELF_NOTE_RWX(s, len) ((void) 0)
#endif
#define GUEST_DESCRIPTION "x86_64 user-mode emulator with AArch64 JIT and built-in proot"   /* --version */
#define GUEST_PLATFORM "x86_64"                                 /* AT_PLATFORM */
#define GUEST_HWCAP 0x078bfbff                                  /* AT_HWCAP: CPUID.1:EDX of the CPU pemu reports */
static void guest_install_tramp(void) {                         /* g_tramp: "mov eax, 15; syscall" = rt_sigreturn, the signal return address */
    memcpy((void*) g_tramp, "\xb8\x0f\x00\x00\x00\x0f\x05", 7);
}
static void guest_cpu_start(u64 sp, u64 entry) {                /* register state of a new process */
    cpu.r[RSP] = sp; cpu.rip = entry; cpu.rf = 0x202; cpu.mxcsr = 0x1f80; cpu.fcw = 0x37f; cpu.fsw = 0; cpu.top = 0;
}
#define GUEST_PC(c) ((c)->rip)                                  /* guest program counter, for the fault log */
static inline void guest_set_sp(CPU*c, u64 sp) { c->r[RSP] = sp; }        /* clone: stack of the child */
static inline void guest_set_tls(CPU*c, u64 tls) { c->fs = tls; }         /* clone: CLONE_SETTLS */
static inline void guest_set_retval(CPU*c, u64 v) { c->r[RAX] = v; }      /* clone: the child sees 0 */
