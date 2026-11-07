/* src/compat/crt0.s — M6 minimal crt0 for the R36S build (owner ruling).
 * The toolchain's Scrt1.o bakes __libc_start_main@GLIBC_2.34, which ArkOS
 * (glibc 2.28) cannot resolve; linked with -nostartfiles in its place.
 * .symver pins the call to __libc_start_main@GLIBC_2.17, the version that
 * alias has had on aarch64 since its introduction. */

    .text
    .globl  _start
    .type   _start, %function
    .symver __libc_start_main_ref, __libc_start_main@GLIBC_2.17

_start:
    /* __libc_start_main(main, argc, argv, init, fini, rtld_fini, stack_end);
     * init/fini/rtld_fini are NULL — glibc provides its own defaults. */
    ldr     x0, =main
    ldr     x1, [sp]        /* argc (kernel puts it at sp) */
    add     x2, sp, #8      /* argv */
    mov     x3, #0          /* init */
    mov     x4, #0          /* fini */
    mov     x5, #0          /* rtld_fini */
    mov     x6, sp          /* stack_end */
    bl      __libc_start_main_ref
    bl      abort           /* not reached */

    .size   _start, . - _start
    .section .note.GNU-stack,"",%progbits
