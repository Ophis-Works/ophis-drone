/* src/compat/glibc_symver.c — glibc-2.28 symbol pinning.
 * ArkOS userspace is
 * Ubuntu-19.10-lineage, glibc 2.28.
 *
 * The host cross toolchain (aarch64-linux-gnu-gcc 15.2, glibc >= 2.39)
 * binds a handful of symbols to versions NEWER than the R36S has:
 *   pow, log2            -> @GLIBC_2.29
 *   pthread_create/join  -> @GLIBC_2.34
 *   __libc_start_main    -> @GLIBC_2.34 (see src/compat/crt0.s)
 *
 * The handheld link uses -Wl,--wrap on these four (chosen over -D macros
 * because renaming `pow`/`log2` breaks glibc's vector-math declarations in
 * math.h). The wrappers below are what every wrapped call site resolves to,
 * and each calls the GLIBC_2.17 alias pinned via .symver — the version that
 * symbol has had on aarch64 since its introduction. All other symbols in
 * the binary already bind @GLIBC_2.17. Gate: tools/handheld_gate.sh
 * (readelf -V must show no required version above GLIBC_2.28).
 */

#include <math.h>
#include <pthread.h>
#include <stdint.h>

__asm__(".symver compat_pow_ref,   pow@GLIBC_2.17\n"
        ".symver compat_log2_ref,  log2@GLIBC_2.17\n"
        ".symver compat_exp_ref,   exp@GLIBC_2.17\n"
        ".symver compat_pthread_create_ref, pthread_create@GLIBC_2.17\n"
        ".symver compat_pthread_join_ref,   pthread_join@GLIBC_2.17\n"
        ".symver compat_strtol_ref,    strtol@GLIBC_2.17\n"
        ".symver compat_strtoumax_ref, strtoumax@GLIBC_2.17\n");

/* The *_ref names are assembler-level aliases of the versioned glibc
 * symbols (rewritten by the .symver directives above); declare them so the
 * calls below type-check. */
extern double compat_pow_ref(double x, double y);
extern double compat_log2_ref(double x);
extern double compat_exp_ref(double x);
extern int    compat_pthread_create_ref(pthread_t *thread,
                                        const pthread_attr_t *attr,
                                        void *(*start_routine)(void *),
                                        void *arg);
extern int    compat_pthread_join_ref(pthread_t thread, void **retval);
extern long      compat_strtol_ref(const char *nptr, char **endptr,
                                   int base);
extern uintmax_t compat_strtoumax_ref(const char *nptr, char **endptr,
                                      int base);

/* Linker-called via -Wl,--wrap; prototypes satisfy -Wmissing-prototypes. */
double __wrap_pow(double x, double y);
double __wrap_log2(double x);
double __wrap_exp(double x);
int    __wrap_pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                             void *(*start_routine)(void *), void *arg);
int    __wrap_pthread_join(pthread_t thread, void **retval);

double __wrap_pow(double x, double y)
{
    return compat_pow_ref(x, y);
}

double __wrap_log2(double x)
{
    return compat_log2_ref(x);
}

double __wrap_exp(double x)
{
    return compat_exp_ref(x);
}

int __wrap_pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                          void *(*start_routine)(void *), void *arg)
{
    return compat_pthread_create_ref(thread, attr, start_routine, arg);
}

int __wrap_pthread_join(pthread_t thread, void **retval)
{
    return compat_pthread_join_ref(thread, retval);
}

/* C23 strtoint shims: the host glibc >= 2.39 headers redirect
 * strtol/strtoumax to the __isoc23_* aliases under strict -std=c23, and
 * those symbols exist only since glibc 2.38 (R36S has 2.28). Defining
 * them here intercepts the references at link time — no new -Wl,--wrap
 * entries are possible without a Makefile change — and the .symver pins
 * above route the calls to the GLIBC_2.17 versions (identical for
 * base != 0/2 parsing; the C23 additions concern base 0/2 "0b" binary
 * literals only). */
long __isoc23_strtol(const char *nptr, char **endptr, int base);
uintmax_t __isoc23_strtoumax(const char *nptr, char **endptr, int base);

long __isoc23_strtol(const char *nptr, char **endptr, int base)
{
    return compat_strtol_ref(nptr, endptr, base);
}

uintmax_t __isoc23_strtoumax(const char *nptr, char **endptr, int base)
{
    return compat_strtoumax_ref(nptr, endptr, base);
}
