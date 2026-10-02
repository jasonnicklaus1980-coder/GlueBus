/* MPC Arcade: glibc compatibility for the helper programs (mpc_stubs.c without the EGL / fontconfig stubs).
   The cross compiler targets glibc 2.39, the MPC X has 2.34: these definitions satisfy the newer symbols inside the
   executable, calling the old (GLIBC_2.4) versions that MPC OS provides. */
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <wchar.h>
__asm__(".symver fmod_old, fmod@GLIBC_2.4");
__asm__(".symver fmodf_old, fmodf@GLIBC_2.4");
__asm__(".symver hypot_old, hypot@GLIBC_2.4");
double fmod_old(double, double); float fmodf_old(float, float); double hypot_old(double, double);
double fmod(double a, double b) { return fmod_old(a, b); }
float fmodf(float a, float b) { return fmodf_old(a, b); }
double hypot(double a, double b) { return hypot_old(a, b); }

long strtol(const char *, char **, int); long long strtoll(const char *, char **, int);
unsigned long strtoul(const char *, char **, int); unsigned long long strtoull(const char *, char **, int);
long __isoc23_strtol(const char *s, char **e, int b) { return strtol(s, e, b); }
long long __isoc23_strtoll(const char *s, char **e, int b) { return strtoll(s, e, b); }
unsigned long __isoc23_strtoul(const char *s, char **e, int b) { return strtoul(s, e, b); }
unsigned long long __isoc23_strtoull(const char *s, char **e, int b) { return strtoull(s, e, b); }
int __isoc23_vsscanf(const char *s, const char *f, va_list ap) { return vsscanf(s, f, ap); }
int __isoc23_sscanf(const char *s, const char *f, ...) { va_list ap; va_start(ap, f); int r = vsscanf(s, f, ap); va_end(ap); return r; }
int __isoc23_fscanf(FILE *fp, const char *f, ...) { va_list ap; va_start(ap, f); int r = vfscanf(fp, f, ap); va_end(ap); return r; }

long getrandom(void *, size_t, unsigned);
unsigned int arc4random(void) { unsigned int v = 0; if (getrandom(&v, sizeof v, 0) != sizeof v) { static unsigned s = 2463534242u; s ^= s << 13; s ^= s >> 17; s ^= s << 5; v = s; } return v; }

size_t strlcpy(char *d, const char *s, size_t n) { size_t l = 0; while (s[l]) ++l; if (n) { size_t c = l < n - 1 ? l : n - 1; for (size_t i = 0; i < c; ++i) d[i] = s[i]; d[c] = 0; } return l; }
size_t strlcat(char *d, const char *s, size_t n) { size_t dl = 0; while (dl < n && d[dl]) ++dl; if (dl == n) { size_t l = 0; while (s[l]) ++l; return n + l; } return dl + strlcpy(d + dl, s, n - dl); }
size_t wcslcpy(wchar_t *d, const wchar_t *s, size_t n) { size_t l = 0; while (s[l]) ++l; if (n) { size_t c = l < n - 1 ? l : n - 1; for (size_t i = 0; i < c; ++i) d[i] = s[i]; d[c] = 0; } return l; }
size_t wcslcat(wchar_t *d, const wchar_t *s, size_t n) { size_t dl = 0; while (dl < n && d[dl]) ++dl; if (dl == n) { size_t l = 0; while (s[l]) ++l; return n + l; } return dl + wcslcpy(d + dl, s, n - dl); }
