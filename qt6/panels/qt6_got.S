/* Qt code is compiled with -mcmodel=large -fPIC and addresses the GOT via
 * the r15 base register (set by PLT stubs when running as a shared library).
 * The statically-linked kernel never runs those stubs, so r15 must be set to
 * the GOT base (start of .got.plt) before any Qt code executes.
 *
 * The Qt code expects r15 to hold the GOT base for the duration of its call
 * tree, but r15 is also a callee-saved register in the SysV ABI: any caller
 * (e.g. kernel_main) may legitimately keep a live value in r15 across this
 * call and must have it restored on return.  So save the caller's r15 first,
 * set r15/r11 to the GOT base for the Qt call tree, then restore the caller's
 * r15 on the way out instead of tail-jumping.  __got_plt_base is provided by
 * linker.ld. */

.section .text

.global qt6_panels_init
.type qt6_panels_init, @function
qt6_panels_init:
    push %r15
    movabsq $__got_plt_base, %r15
    movabsq $__got_plt_base, %r11
    call qt6_panels_init_cpp
    pop %r15
    ret

.global qt6_panels_run
.type qt6_panels_run, @function
qt6_panels_run:
    push %r15
    movabsq $__got_plt_base, %r15
    movabsq $__got_plt_base, %r11
    call qt6_panels_run_cpp
    pop %r15
    ret

.global qt6_panels_stop
.type qt6_panels_stop, @function
qt6_panels_stop:
    push %r15
    movabsq $__got_plt_base, %r15
    movabsq $__got_plt_base, %r11
    call qt6_panels_stop_cpp
    pop %r15
    ret

.global qt6_panels_active
.type qt6_panels_active, @function
qt6_panels_active:
    push %r15
    movabsq $__got_plt_base, %r15
    movabsq $__got_plt_base, %r11
    call qt6_panels_active_cpp
    pop %r15
    ret

.section .note.GNU-stack,"",@progbits
