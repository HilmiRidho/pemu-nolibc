/* Differential test of the libc-free runtime (host/rt/): this file is built twice, once against host/rt/ (-DPEMU_RT_TEST, no libc) and once against the system libc,
 * and both must print exactly the same.  tests/integration/rt_libc.sh does the build and the diff.  usage: rt_libc_test TMPDIR */
#ifdef PEMU_RT_TEST
#include "host/pemu_host.h"
#else
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#endif

static unsigned long long rs = 88172645463325252ULL;
static unsigned long long rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return rs; }
static double dbl(unsigned long long b) { double d; memcpy(&d, &b, 8); return d; }
static int cmpi(const void *a, const void *b) { return *(const int *) a - *(const int *) b; }
static int cmpbig(const void *a, const void *b) { return ((const unsigned char *) a)[0] - ((const unsigned char *) b)[0]; }
struct big { unsigned char k[1000]; };

int main(int argc, char **argv) {
    static const char *const fm[] = { "%f", "%.0f", "%.2f", "%10.3f", "%012.3f", "%-12.4f|", "%e", "%.0e", "%.3e", "%E", "%#.0e", "%g", "%.10g", "%#g", "%G", "%.0g",
                                      "%+.5g", "% g", "%.20f", "%.40e", "%#.0f", "%F", "%.1100f", "%.1300e", "%#.1200g", "%.1100g", "%-1200.1090f|", "%.1074f", "%.1075f" };
    double sp[] = { 0.0, -0.0, 0.5, 1.5, 2.5, 0.05, 0.15, 9.9995, 99999.5, 999999.5, 1e22, 1e23, 123456789012345678.0, 1e-5, 0.0001, 0.00001234, 100000.0, 1e100, 1.7976931348623157e308,
                    2.2250738585072014e-308, 4.9406564584124654e-324, 1.0 / 3, 2.0 / 3, 1e15, 1e16, 5e-324 * 3, __builtin_inf(), -__builtin_inf(), __builtin_nan(""), -__builtin_nan("") };
    for (unsigned i = 0; i < sizeof sp / sizeof *sp; i++) for (unsigned j = 0; j < sizeof fm / sizeof *fm; j++) {
        if (!strcmp(fm[j], "%#g") && sp[i] == 999999.5) continue;                 /* glibc prints "1.e+06" here (a glibc bug); C says 1.00000e+06, which host/rt/ prints */
        char b[2000]; int r = snprintf(b, sizeof b, fm[j], sp[i]);
        if (strlen(b) > 120) { printf("%s #%u: %d chars, head %.30s tail %s\n", fm[j], i, r, b, b + strlen(b) - 20); } else printf("%s #%u: %d [%s]\n", fm[j], i, r, b);
    }
    for (int i = 0; i < 6000; i++) {                                 /* random bit patterns (every exponent) and random "ordinary" values */
        double d = i % 2 ? dbl(rnd()) : (double) (long long) (rnd() >> 20) / (double) (1ULL << (rnd() % 40));
        if (d != d) continue;
        char b[2000]; const char *f = fm[rnd() % (sizeof fm / sizeof *fm)];
        int r = snprintf(b, sizeof b, f, d);
        if (strlen(b) > 120) printf("%s: %d chars, head %.30s tail %s\n", f, r, b, b + strlen(b) - 20); else printf("%s: %d [%s]\n", f, r, b);
    }
    { char b[64]; snprintf(b, sizeof b, "%*.*f|%-*.*e|%.*g", 12, 3, 3.14159, 14, 2, 31415.9, 4, 0.000123456); printf("%s\n", b); }

    /* sscanf */
    { int a = -1, b = -1; char s[32] = "unset", t[32] = "unset"; double d = -1; float fl = -1; long double ld = -1; unsigned x = 0; void *p = 0; int r;
      r = sscanf("12abc", "%i%s", &a, s);              printf("1: %d %d %s\n", r, a, s);
      r = sscanf("0x1fz", "%i%s", &a, s);              printf("2: %d %d %s\n", r, a, s);
      r = sscanf("0755 9", "%i %i", &a, &b);           printf("3: %d %d %d\n", r, a, b);
      r = sscanf("  -42,17", "%d,%d", &a, &b);         printf("4: %d %d %d\n", r, a, b);
      r = sscanf("ff 0xFF 10", "%x %x %o", &x, &a, &b); printf("5: %d %u %d %d\n", r, x, a, b);
      r = sscanf("3.5 -0.25e2 1e3", "%lf %lf %lf", &d, &ld, &d); printf("6: %d %.17g %.17Lg\n", r, d, ld);
      r = sscanf("7.25x", "%f%s", &fl, s);             printf("7: %d %.9g %s\n", r, fl, s);
      r = sscanf("inf -inf nan", "%lf %lf %lf", &d, &d, &d); printf("9: %d\n", r);
      r = sscanf("abc,def", "%[a-z],%[^,]", s, t);     printf("10: %d %s %s\n", r, s, t);
      r = sscanf("]x]y", "%[]x]%s", s, t);             printf("11: %d %s %s\n", r, s, t);
      r = sscanf("key=value rest", "%[^=]=%s", s, t);  printf("12: %d %s %s\n", r, s, t);
      r = sscanf("xyz", "%[0-9]", s);                  printf("13: %d\n", r);
      r = sscanf("0x1000", "%p", &p);                  printf("14: %d %lx\n", r, (unsigned long) p);
      r = sscanf("123456", "%3d%d", &a, &b);           printf("15: %d %d %d\n", r, a, b);
      r = sscanf("12.5 99", "%*f %d", &a);             printf("16: %d %d\n", r, a); }

    /* sscanf %lf: correct rounding of the decimal text (the bits are printed, so one ulp shows) */
    { static const char *const ds[] = { "0.1", "0.3", "1e23", "8.5e-5", "9007199254740993", "9007199254740995", "9007199254740992.5", "9007199254740993.0000000000000000000000001",
          "18014398509481985", "2.2250738585072011e-308", "2.2250738585072012e-308", "2.2250738585072014e-308", "4.9406564584124654e-324", "2.4703282292062327e-324",
          "2.4703282292062328e-324", "2.4703282292062329e-324", "7.4109846876186981e-324", "7.4109846876186982e-324", "1.7976931348623157e308", "1.7976931348623158e308",
          "1.7976931348623159e308", "1e309", "1e400", "1e-400", "1e99999999", "-1e-99999999", "123456789012345678901234567890", "0.000000000000000000000000000001234567890123456789",
          "1.00000000000000011102230246251565404236316680908203125", "1.00000000000000011102230246251565404236316680908203126", "1.00000000000000011102230246251565404236316680908203124",
          "-0", "-0.0e5", ".5", "5.", "+.5e1", "00000000000000000000001.5", "1e+", "1e", "0x10", "0x1p-1074", "0x1.8p1", "0x.8", "-0x.8p4", "0x1p-1075", "0x1.00000000000008p0",
          "0x1.00000000000018p0", "0x1.000000000000080000001p0", "0x1.fffffffffffffp1023", "0x1.fffffffffffff8p1023", "0xFFFFFFFFFFFFFFFFFp0", "0x1P+3", "0x", "0xg", "0x1p", "0X1.8" };
      for (unsigned i = 0; i < sizeof ds / sizeof *ds; i++) { double d = -7; unsigned long long b; int r = sscanf(ds[i], "%lf", &d); memcpy(&b, &d, 8); printf("lf %u: %d %016llx\n", i, r, b); }
      static char big[4000]; int bl;                                     /* long texts: the tie at 2^53 + 1 is decided by a 1 that is 850 digits out */
      bl = snprintf(big, sizeof big, "9007199254740993."); memset(big + bl, '0', 850); strcpy(big + bl + 850, "1");
      { double d = -7; unsigned long long b; int r = sscanf(big, "%lf", &d); memcpy(&b, &d, 8); printf("long tail 1: %d %016llx\n", r, b); }
      big[bl + 850] = 0;
      { double d = -7; unsigned long long b; int r = sscanf(big, "%lf", &d); memcpy(&b, &d, 8); printf("long tail 0: %d %016llx\n", r, b); }
      bl = snprintf(big, sizeof big, "0."); memset(big + bl, '0', 700); strcpy(big + bl + 700, "5e700");
      { double d = -7; unsigned long long b; int r = sscanf(big, "%lf", &d); memcpy(&b, &d, 8); printf("long lead zeros: %d %016llx\n", r, b); }
      { double d = -7; unsigned long long b; int r = sscanf("3.14159265358979", "%5lf", &d); memcpy(&b, &d, 8); printf("width: %d %016llx\n", r, b); }
      { double d = -7; unsigned long long b; int r = sscanf("1.5e+20", "%5lf", &d); memcpy(&b, &d, 8); printf("width in exponent: %d %016llx\n", r, b); }
      for (int i = 0; i < 6000; i++) {
          char tx[200]; double d = -7; unsigned long long b; int m = i % 3;
          if (m == 0) { double v = dbl(rnd()); if (v != v || v - v != 0) continue; snprintf(tx, sizeof tx, "%.17g", v); }          /* shortest-ish round trip text */
          else if (m == 1) { double v = dbl(rnd()); if (v != v || v - v != 0) continue; snprintf(tx, sizeof tx, "%.30e", v); }      /* more digits than needed */
          else { int nd = 1 + (int) (rnd() % 40), o = 0, pt = (int) (rnd() % (unsigned) (nd + 1)); if (rnd() % 2) tx[o++] = '-';  /* random digits, point, exponent */
                 for (int k = 0; k < nd; k++) { if (k == pt) tx[o++] = '.'; tx[o++] = (char) ('0' + rnd() % 10); }
                 snprintf(tx + o, sizeof tx - (size_t) o, "e%d", (int) (rnd() % 700) - 350); }
          int r = sscanf(tx, "%lf", &d); memcpy(&b, &d, 8); printf("rt %s: %d %016llx\n", tx, r, b);
      } }

    /* FILE: a stream opened r+ / w+ / a+ */
    if (argc > 1) {
        char path[256]; snprintf(path, sizeof path, "%s/f.txt", argv[1]); char line[64], buf[256]; FILE *fp; long n;
        fp = fopen(path, "w"); fputs("hello\nworld\n", fp); fclose(fp);
        fp = fopen(path, "r+"); fgets(line, sizeof line, fp); fclose(fp);                          /* only a read: the file must not change */
        fp = fopen(path, "r"); n = (long) fread(buf, 1, sizeof buf - 1, fp); buf[n] = 0; fclose(fp); printf("r+ read, close: %ld [%s]\n", n, buf);
        fp = fopen(path, "r+"); fgets(line, sizeof line, fp); fflush(fp); fputs("WORLD", fp); long t1 = ftell(fp); fclose(fp);    /* read, fflush, write at the right place */
        fp = fopen(path, "r"); n = (long) fread(buf, 1, sizeof buf - 1, fp); buf[n] = 0; fclose(fp); printf("r+ read, write: %ld %ld [%s]\n", t1, n, buf);
        fp = fopen(path, "w+"); fputs("abcdef", fp); fseek(fp, 0, SEEK_SET); n = (long) fread(buf, 1, 3, fp); buf[n] = 0; printf("w+: [%s] tell %ld\n", buf, ftell(fp));
        fputs("XY", fp); fseek(fp, 0, SEEK_SET); n = (long) fread(buf, 1, 10, fp); buf[n] = 0; printf("w+ after write: [%s]\n", buf); fclose(fp);
        fp = fopen(path, "a+"); fputs("++", fp); fseek(fp, 0, SEEK_SET); n = (long) fread(buf, 1, 20, fp); buf[n] = 0; printf("a+: [%s]\n", buf); fclose(fp);
        unlinkat(-100, path, 0);                                                                     /* AT_FDCWD: the runtime has unlinkat only */
    }

    /* qsort: small and large elements */
    { int v[9] = { 5, 3, 9, 1, 7, 3, 8, 0, 2 }; qsort(v, 9, sizeof v[0], cmpi); for (int i = 0; i < 9; i++) printf("%d ", v[i]); printf("\n");
      static struct big a[50]; for (int i = 0; i < 50; i++) { a[i].k[0] = (unsigned char) ((i * 37) % 50); memset(a[i].k + 1, (int) a[i].k[0], 999); }
      qsort(a, 50, sizeof a[0], cmpbig); int ok = 1; for (int i = 0; i < 50; i++) if (a[i].k[0] != i || a[i].k[999] != i) ok = 0; printf("qsort 1000-byte elements: %s\n", ok ? "sorted" : "NOT sorted"); }

    /* posix_memalign: alignments, contents kept by realloc, free of an aligned block */
    { int ok = 1; size_t als[] = { 8, 16, 32, 64, 256, 4096, 65536 };
      for (unsigned i = 0; i < sizeof als / sizeof *als; i++) for (size_t n = 1; n < 300000; n = n * 7 + 3) {
          void *p = 0; if (posix_memalign(&p, als[i], n) != 0 || ((unsigned long) p & (als[i] - 1))) { ok = 0; continue; }
          memset(p, 0xA5, n); unsigned char *q = realloc(p, n + 100); if (!q) { ok = 0; continue; }
          for (size_t k = 0; k < n; k += 97) if (q[k] != 0xA5) ok = 0;
          free(q); }
      void *p = 0; printf("posix_memalign: %s; align 3 -> %d, align 0 -> %d\n", ok ? "ok" : "BAD", posix_memalign(&p, 3, 8), posix_memalign(&p, 0, 8)); }

    /* malloc / posix_memalign / realloc / free mixed at random: no block overlaps another (each has its own fill byte), alignments hold, realloc keeps the contents */
    { enum { M = 48 }; static void *p[M]; static size_t sz[M], al[M]; static unsigned char pat[M]; long bad = 0, ops = 0;
      for (int it = 0; it < 30000; it++) {
          int k = (int) (rnd() % M); unsigned char *q = p[k];
          if (q) {
              if ((unsigned long) q & (al[k] - 1)) bad++;
              for (size_t j = 0; j < sz[k]; j += 29) if (q[j] != pat[k]) bad++;
              if (q[sz[k] - 1] != pat[k]) bad++;
              if (rnd() % 3 == 0) { size_t n = rnd() % 5000 + 1; q = realloc(q, n); if (!q) { bad++; p[k] = 0; continue; }
                  for (size_t j = 0; j < (n < sz[k] ? n : sz[k]); j += 29) if (q[j] != pat[k]) bad++;
                  memset(q, pat[k], n); p[k] = q; sz[k] = n; al[k] = 16; }
              else { free(q); p[k] = 0; }
              ops++;
          } else {
              size_t n = rnd() % 8 == 0 ? rnd() % 300000 + 1 : rnd() % 4000 + 1; int mem = rnd() % 3 != 0;
              al[k] = mem ? (size_t) 8 << (rnd() % 14) : 16;
              void *r = 0; if (mem ? posix_memalign(&r, al[k], n) != 0 : !(r = malloc(n))) { bad++; continue; }
              pat[k] = (unsigned char) (it % 251 + 1); memset(r, pat[k], n); p[k] = r; sz[k] = n; ops++;
          }
      }
      for (int k = 0; k < M; k++) if (p[k]) free(p[k]);
      printf("malloc stress: %ld operations, %ld bad\n", ops, bad); }
    fflush(stdout); return 0;
}
