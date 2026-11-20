/* host/abi/include/stddef.h - libc-free header set. SPDX-License-Identifier: Apache-2.0 */
#ifndef PEMU_ABI_STDDEF_H
#define PEMU_ABI_STDDEF_H
typedef __SIZE_TYPE__ size_t; typedef __PTRDIFF_TYPE__ ptrdiff_t; typedef __WCHAR_TYPE__ wchar_t;
typedef struct { long long __ll __attribute__((aligned(__alignof__(long long)))); long double __ld __attribute__((aligned(__alignof__(long double)))); } max_align_t;
#ifndef NULL
#define NULL ((void *) 0)
#endif
#ifndef offsetof
#define offsetof(t, m) __builtin_offsetof(t, m)
#endif
#define PTRDIFF_MAX __PTRDIFF_MAX__
#endif
