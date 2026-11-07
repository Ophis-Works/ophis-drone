/* src/compat/glibc_symver.c — M6 glibc-2.28 symbol pinning (owner ruling,
 * supersedes the M0 static-link decision; PLAN.md §1: ArkOS userspace is
 * Ubuntu-19.10-lineage, glibc 2.28).
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

__asm__(".symver compat_pow_ref,   pow@GLIBC_2.17\n"
        ".symver compat_log2_ref,  log2@GLIBC_2.17\n"
        ".symver compat_exp_ref,   exp@GLIBC_2.17\n"
        ".symver compat_pthread_create_ref, pthread_create@GLIBC_2.17\n"
        ".symver compat_pthread_join_ref,   pthread_join@GLIBC_2.17\n");

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
