#ifndef CODEOS_ARM64_UI_H
#define CODEOS_ARM64_UI_H

/* CodeOS ARM64 shell/desktop.
 *
 * A small, allocator-free interface layered on the ARM64 framebuffer and
 * input HAL.  It is the first step toward the CodeOS ARM laptop desktop: it
 * uses the same serial console, ramfb surface and virtio input path as the
 * x86_64 build, without pulling in the x86_64-only userspace/Qt stack.
 */
void codeos_ui_run(void) __attribute__((noreturn));

#endif
