/* host/abi/include/limits.h - libc-free header set. SPDX-License-Identifier: Apache-2.0 */
#ifndef PEMU_ABI_LIMITS_H
#define PEMU_ABI_LIMITS_H
#define CHAR_BIT 8
#define SCHAR_MIN (-128)
#define SCHAR_MAX 127
#define UCHAR_MAX 255
#define CHAR_MIN (-128)
#define CHAR_MAX 127
#define SHRT_MIN (-32768)
#define SHRT_MAX 32767
#define USHRT_MAX 65535
#define INT_MAX __INT_MAX__
#define INT_MIN (-INT_MAX - 1)
#define UINT_MAX (INT_MAX * 2U + 1U)
#define LONG_MAX __LONG_MAX__
#define LONG_MIN (-LONG_MAX - 1L)
#define ULONG_MAX (LONG_MAX * 2UL + 1UL)
#define LLONG_MAX __LONG_LONG_MAX__
#define LLONG_MIN (-LLONG_MAX - 1LL)
#define ULLONG_MAX (LLONG_MAX * 2ULL + 1ULL)
#define SSIZE_MAX LONG_MAX
#define PATH_MAX 4096
#define NAME_MAX 255
#define PIPE_BUF 4096
#define IOV_MAX 1024
#define NGROUPS_MAX 65536
#define LINE_MAX 2048
#define HOST_NAME_MAX 64
#endif
