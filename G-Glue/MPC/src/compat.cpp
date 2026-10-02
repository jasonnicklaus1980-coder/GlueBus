// glibc compatibility for the MPC (glibc 2.34). The statically linked C++ runtime from a newer toolchain asks for
// arc4random (glibc 2.36) and __isoc23_strtoul (2.38); these hidden definitions satisfy them inside the plugin with
// functions MPC OS has. Only built into the ARM plugin.
#include <cstddef>
#include <sys/random.h>
#define HIDDEN extern "C" __attribute__ ((visibility ("hidden")))
__asm__ (".symver strtoul_old, strtoul@GLIBC_2.4");
extern "C" unsigned long strtoul_old (const char*, char**, int);
HIDDEN unsigned long __isoc23_strtoul (const char* s, char** e, int b) { return strtoul_old (s, e, b); }
HIDDEN unsigned int arc4random (void)
{
    unsigned int v = 0;
    if (getrandom (&v, sizeof v, 0) != (long) sizeof v) { static unsigned int x = 2463534242u; x ^= x << 13; x ^= x >> 17; x ^= x << 5; v = x; }
    return v;
}
